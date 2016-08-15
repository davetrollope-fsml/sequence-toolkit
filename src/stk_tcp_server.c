#include "stk_data_flow_api.h"
#include "stk_data_flow.h"
#include "stk_common.h"
#include "stk_internal.h"
#include "stk_env.h"
#include "stk_sequence.h"
#include "stk_sequence_api.h"
#include "stk_tcp_server_api.h"
#include "stk_tcp.h"
#include "stk_tcp_internal.h"
#include "stk_options_api.h"
#include "stk_ports.h"

#define STK_DUMP_RCV_HEX 1

#ifdef __APPLE_
#define _DARWIN_C_SOURCE
#endif

#ifndef __APPLE__
/* Apple doesn't allow mixed mode blocking on sockets and relies on HUP from poll
 * (using non blocking all the time). Thus apple can't use zero length reads to
 * indicate the end of a connection, but other platforms can.
 */
#define ZEROREAD_RETURNS
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <limits.h>

#define STK_TCP_BACKLOG 1024

#define STK_MAX_IOV IOV_MAX
#ifdef __APPLE__
#define STK_NB_SEND_FLAGS 0
#else
#define STK_NB_SEND_FLAGS MSG_NOSIGNAL
#endif

#define STK_CACHED_READBUF_SZ 64*1024

#ifdef __CYGWIN__
#define STK_DEBUG_BUFFER(_pfx,_readbuf) \
	STK_DEBUG(STKA_NET,"%s buffer %p start %llu read %llu sz %llu",_pfx,(_readbuf),(_readbuf)->elem_start,(_readbuf)->read,(_readbuf)->sz);
#else
#define STK_DEBUG_BUFFER(_pfx,_readbuf) \
	STK_DEBUG(STKA_NET,"%s buffer %p start %lu read %lu sz %lu",_pfx,(_readbuf),(_readbuf)->elem_start,(_readbuf)->read,(_readbuf)->sz);
#endif

stk_ret stk_tcp_server_data_flow_send(stk_data_flow_t *flow,stk_sequence_t *data_sequence,stk_uint64 flags);
stk_ret stk_tcp_server_destroy_data_flow(stk_data_flow_t *flow);
stk_sequence_t *stk_tcp_server_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);
stk_ret stk_tcp_shift_buf(stk_tcp_wire_read_buf_t *readbuf);
stk_ret stk_tcp_server_data_flow_buffered(stk_data_flow_t *flow);
char *stk_tcp_server_data_flow_protocol(stk_data_flow_t *flow);
stk_ret stk_send_vector(stk_data_flow_t *df,struct iovec *vectors,int num_chunks,int start_idx,stk_uint64 flags);

static stk_data_flow_module_t tcp_server_fptrs = {
	stk_tcp_server_create_data_flow, stk_tcp_server_destroy_data_flow,
	stk_tcp_server_data_flow_send, stk_tcp_server_data_flow_rcv,
	stk_tcp_server_data_flow_id_ip, stk_tcp_server_data_flow_buffered,
	stk_tcp_server_data_flow_protocol
};

typedef struct stk_tcp_server_stct {
	int sock;
	short port;
	struct sockaddr_in server_addr;
	struct sockaddr_in client_addr;
	stk_tcp_wire_read_buf_t readbuf;
	stk_data_flow_fd_created_cb fd_created_cb;
	stk_data_flow_fd_destroyed_cb fd_destroyed_cb;
	stk_data_flow_destroyed_cb df_destroyed_cb;
	struct sockaddr_in accept_addr;
} stk_tcp_server_t;

stk_data_flow_t *stk_tcp_server_create_data_flow(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options)
{
	stk_data_flow_t *df = stk_alloc_data_flow(env,STK_TCP_SERVER_FLOW,name,id,sizeof(stk_tcp_server_t),&tcp_server_fptrs,options);
	stk_tcp_server_t *ts = stk_data_flow_module_data(df);
	int rc;

	if(df) {
		short port = STK_DEFAULT_TCP_SERVER_PORT;

		ts->sock = socket(PF_INET, SOCK_STREAM, 0);
		if(ts->sock == -1) {
			STK_LOG(STK_LOG_ERROR,"create server socket for data flow '%s'[%lu], env %p",name,id,env);
			stk_free_data_flow(df);
			return NULL;
		}

		/* Use non blocking mode */
		rc = fcntl(ts->sock, F_SETFL, O_NONBLOCK);
		if(rc == -1) {
			close(ts->sock);
			STK_LOG(STK_LOG_ERROR,"set non blocking mode on socket for data flow '%s'[%lu], env %p",name,id,env);
			stk_free_data_flow(df);
			return NULL;
		}

#ifdef __APPLE__
		{
		int true = 1;
		rc = setsockopt(ts->sock, SOL_SOCKET, SO_NOSIGPIPE, &true, sizeof(true));
		if(rc < 0)
			STK_LOG(STK_LOG_ERROR,"Failed to set server socket no sigpipe, env %p",env);
		}
#endif

		{
		void *bindaddr_str = stk_find_option(options,"bind_address",NULL);
		void *reuseaddr_str = stk_find_option(options,"reuseaddr",NULL);
		void *port_str = stk_find_option(options,"bind_port",NULL);
		void *sndbuf_str = stk_find_option(options,"send_buffer_size",NULL);
		void *rcvbuf_str = stk_find_option(options,"receive_buffer_size",NULL);
		void *nodelay_str = stk_find_option(options,"nodelay",NULL);

		ts->fd_created_cb = (stk_data_flow_fd_created_cb) stk_find_option(options,"fd_created_cb",NULL);
		ts->fd_destroyed_cb = (stk_data_flow_fd_destroyed_cb) stk_find_option(options,"fd_destroyed_cb",NULL);

		ts->server_addr.sin_family = AF_INET;
		ts->df_destroyed_cb = (stk_data_flow_destroyed_cb) stk_find_option(options,"df_destroyed_cb",NULL);

		if(port_str) {
			port = (short) atoi(port_str);
			ts->server_addr.sin_port = htons(port);
		}

		if(bindaddr_str) {
			rc = inet_pton(AF_INET,bindaddr_str,&ts->server_addr.sin_addr);
			if(rc <= 0) {
				STK_LOG(STK_LOG_ERROR,"Could not convert bind address errno %d",errno);
				ts->server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
			}
		} else
			ts->server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

		if(nodelay_str) {
			int true = 1;
			rc = setsockopt(ts->sock, IPPROTO_TCP, TCP_NODELAY, &true, sizeof(true));
			if(rc < 0)
				STK_LOG(STK_LOG_ERROR,"Failed to set server socket no delay on port %d, env %p",port,env);
		}

		if(reuseaddr_str) {
			int true = 1;
			rc = setsockopt(ts->sock, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(true));
			if(rc < 0)
				STK_LOG(STK_LOG_ERROR,"Failed to set server socket to REUSEADDR on port %d, env %p",port,env);
		}

		{
		int sndbuf = 1048576, rcvbuf = 8388608; /* Default to 1MB Send buf, 8MB receive */

		if(sndbuf_str)
			sndbuf = atoi(sndbuf_str);

		if(rcvbuf_str)
			rcvbuf = atoi(rcvbuf_str);

		set_sock_buf_nice(ts->sock, SOL_SOCKET, SO_SNDBUF, sndbuf, port, env);
		set_sock_buf_nice(ts->sock, SOL_SOCKET, SO_RCVBUF, rcvbuf, port, env);

		}

		if(bind(ts->sock,(struct sockaddr *) &ts->server_addr,sizeof(ts->server_addr)) == -1) {
			close(ts->sock);
			STK_LOG(STK_LOG_ERROR,"Failed to bind server socket for data flow '%s'[%lu] to port %d, errno %d %s : env %p",name,id,port,errno,strerror(errno),env);
			stk_free_data_flow(df);
			return NULL;
		}

		if(listen(ts->sock,STK_TCP_BACKLOG) == -1) {
			close(ts->sock);
			STK_LOG(STK_LOG_ERROR,"Failed to listen to server socket for data flow '%s'[%lu] to port %d, errno %d %s : env %p",name,id,port,errno,strerror(errno),env);
			stk_free_data_flow(df);
			return NULL;
		}

		if(ts->fd_created_cb)
			ts->fd_created_cb(df,stk_get_data_flow_id(df),ts->sock);
		}

		return df;
	}

	return NULL;
}

stk_ret stk_tcp_server_destroy_data_flow(stk_data_flow_t *df)
{
	stk_tcp_server_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */

	if(ts->sock) {
		if(ts->fd_destroyed_cb)
			ts->fd_destroyed_cb(df,stk_get_data_flow_id(df),ts->sock);

		close(ts->sock);
	}


	ts->readbuf.sz = 0;
	if(ts->readbuf.buf) STK_FREE(ts->readbuf.buf);

	return stk_free_data_flow(df);
}

int stk_tcp_server_fd(stk_data_flow_t *svr_df)
{
	stk_tcp_server_t *ts = stk_data_flow_module_data(svr_df); /* Asserts on structure type */
	return ts->sock;
}

stk_data_flow_t *stk_tcp_server_accept(stk_data_flow_t *svr_df)
{
	stk_tcp_server_t *sts = stk_data_flow_module_data(svr_df); /* Asserts on structure type */
	struct sockaddr saddr;
	socklen_t slen = sizeof(saddr);
	/* FIXME - I don't think this is doing anything - is df_destroyed_cb ever used now? and its not copied in alloc */
	stk_options_t options[2] = { { "df_destroyed_cb", (void *) sts->df_destroyed_cb }, { NULL, NULL} };
	int oldfd = sts->sock,newfd;

	STK_ASSERT(STKA_NET,sts!=NULL,"Getting server control block for data flow %p",svr_df);

	newfd = accept(sts->sock,&saddr,&slen);
	if(newfd < 0) return NULL;

	{
	stk_data_flow_t *df = stk_alloc_data_flow(stk_env_from_data_flow(svr_df),
		STK_TCP_ACCEPTED_FLOW,stk_data_flow_name(svr_df),stk_get_data_flow_id(svr_df),
		sizeof(stk_tcp_server_t),&tcp_server_fptrs,options);
	stk_tcp_server_t *ts = stk_data_flow_module_data(df);

	memcpy(&ts->accept_addr,&saddr,sizeof(ts->accept_addr));
	memcpy(&ts->client_addr,&ts->accept_addr,sizeof(ts->accept_addr));
	ts->client_addr.sin_port = ntohs(ts->client_addr.sin_port);
	ts->client_addr.sin_addr.s_addr = ntohl(ts->client_addr.sin_addr.s_addr);

	STK_ASSERT(STKA_NET,ts!=NULL,"Getting server control block %p for client %x:%d (%d) fd %d -> %d",df,
				ts->client_addr.sin_addr.s_addr, ts->client_addr.sin_port, ntohs(ts->client_addr.sin_port),oldfd,newfd);
	ts->sock = newfd;
	/* Create a caching read buffer for this data flow */
	ts->readbuf.buf = STK_ALLOC_BUF(STK_CACHED_READBUF_SZ);
	ts->readbuf.sz = STK_CACHED_READBUF_SZ;
	ts->readbuf.df = df;

	if(sts->fd_created_cb)
		sts->fd_created_cb(df,stk_get_data_flow_id(df),ts->sock);
	if(sts->fd_destroyed_cb) /* Carry over destroy callback */
		ts->fd_destroyed_cb = sts->fd_destroyed_cb;

	return df;
	}
	return NULL;
}

void stk_dump_hex(unsigned char *ptr, ssize_t ret, int offset)
{
	/* Dump hex */
	{
	unsigned char line[255];
	int x = 0,xs = 0,line_offset = 0;
	line[0] = 0;
	while(x < ret) {
		line_offset += sprintf((char *) &line[line_offset],"%02x ",*ptr);
		ptr++; x++;
		if (x % 8 == 0) {
			STK_DEBUG(STKA_NET,">[%05x] %s",xs + offset,line);
			line_offset = 0;
			line[0] = 0;
			xs = x;
		}
	}
	if (x % 8 != 0)
		STK_DEBUG(STKA_NET,">[%05x] %s",xs + offset,line);
	}
}

stk_uint64 stk_tcp_server_recv(stk_data_flow_t *df,stk_tcp_wire_read_buf_t *bufread)
{
	stk_tcp_server_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	ssize_t ret;

	stk_set_data_flow_errno(df,0);
	ret = recv(ts->sock,&bufread->buf[bufread->read],bufread->sz - bufread->read,0);
	STK_DEBUG(STKA_NET,"recv df %p fd %d ret %ld errno %d",df,ts->sock,ret,errno);
	STK_DEBUG_BUFFER("",bufread);
	if(ret == -1) {
		if(errno == EBADF) {
			stk_set_data_flow_errno(df,errno);
			STK_LOG(STK_LOG_ERROR,"recv failed, bad fd %d",ts->sock);
			return 0;
		}
		if(errno != EWOULDBLOCK && errno != ECONNRESET && errno != EINTR) {
			stk_set_data_flow_errno(df,errno);
			STK_LOG(STK_LOG_ERROR,"recv failed, errno %d",errno);
		}
		return 0;
	}

	if(STK_DEBUG_FLAG(STKA_HEX))
		stk_dump_hex((unsigned char *) &bufread->buf[bufread->read],ret,bufread->read);

	return (stk_uint64) ret;
}

stk_ret stk_tcp_read_segment_hdr(stk_data_flow_t *df,stk_tcp_wire_read_buf_t *readbuf)
{
	stk_tcp_server_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	stk_sequence_iterator_t *iter = readbuf->seqiter;
	stk_uint64 bytes_read = 0;

	/* Temporarily clear the iterator from being used to receive the header*/
	readbuf->seqiter = NULL;

	while(stk_tcp_data_buffered(readbuf) < sizeof(readbuf->segment_hdr)) {
		bytes_read = stk_tcp_server_recv(df,readbuf);
		if(bytes_read == 0) {
			readbuf->cb_rc = !STK_SUCCESS;
			readbuf->seqiter = iter;
			return STK_SUCCESS;
		}
		readbuf->read += bytes_read;
		if(readbuf->read == readbuf->sz)
			STK_ASSERT(STKA_NET,stk_tcp_shift_buf(&ts->readbuf)==STK_SUCCESS,"shift readbuf down %p %lu",readbuf,readbuf->elem_start);
	}
	memcpy(&readbuf->segment_hdr,&ts->readbuf.buf[ts->readbuf.elem_start],sizeof(readbuf->segment_hdr));
	ts->readbuf.elem_start += sizeof(readbuf->segment_hdr);

	readbuf->seqiter = iter;
	return STK_SUCCESS;
}

stk_ret stk_tcp_fill_segment(void *data,stk_data_flow_t *df,stk_tcp_wire_read_buf_t *preread)
{
	stk_tcp_wire_read_buf_t bufread;
	stk_tcp_server_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	stk_uint64 bytes_read = 0;
	stk_uint64 offset = stk_tcp_data_buffered(preread);

	/* Limit read to size of segment if preread data is more than the segment */
	if(offset > preread->segment_hdr.segment_len) offset = preread->segment_hdr.segment_len;

	memset(&bufread,0,sizeof(bufread));

	memcpy(data,&preread->buf[preread->elem_start],offset);
	preread->elem_start += offset;

	if(offset < preread->segment_hdr.segment_len) {
		/* Now read the rest of the data enbulk in to the destination buffer */
		bufread.sz = preread->segment_hdr.segment_len - offset;
		bufread.read = 0;
		bufread.buf = ((char*)data) + offset;
		STK_DEBUG(STKA_NET,"data %p buf %p offset %lu sz %lu segment_len %d\n",data,bufread.buf,offset,bufread.sz,preread->segment_hdr.segment_len);

		while(bufread.read < bufread.sz) {
			bytes_read = stk_tcp_server_recv(df,&bufread);
			if(bytes_read == 0) {
				preread->cb_rc = !STK_SUCCESS;
				return STK_SUCCESS;
			}
			bufread.read += bytes_read;
		}
		STK_DEBUG(STKA_NET,"read %lu\n",bufread.read);
	}
	return STK_SUCCESS;
}

stk_ret stk_tcp_server_segment_rcv_cb(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	stk_tcp_wire_read_buf_t *readbuf = (stk_tcp_wire_read_buf_t *) clientd;
	stk_data_flow_t *df = readbuf->df;
	stk_tcp_server_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	stk_ret rc;

	/* Read the segment header, then the data */
	rc = stk_tcp_read_segment_hdr(df,readbuf);
	if(rc != STK_SUCCESS || readbuf->cb_rc != STK_SUCCESS) return rc;

	STK_DEBUG(STKA_NET,"segment_rcv_cb segments len %d",readbuf->segment_hdr.segment_len);

	/* Set the segment's user type */
	user_type = readbuf->segment_hdr.user_type;
	rc = stk_sequence_iterator_set_user_type(readbuf->seqiter,user_type);
	STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"set user type by iterator rc %d",rc);

	if(readbuf->segment_idx > 0 && readbuf->segment_hdr.segment_id != readbuf->last_sgmt_id) {
		stk_tcp_server_t *ts = stk_data_flow_module_data(df);
		STK_LOG(STK_LOG_ERROR,"Segment ID to read %d is unexpectedly different than previous segment %d in sequence, idx %d. TCP fd %d for data flow %s[%lu], env %p",
			readbuf->segment_hdr.segment_id,readbuf->last_sgmt_id,readbuf->segment_idx,ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df));
		return STK_NETERR;
	}
	readbuf->last_sgmt_id = readbuf->segment_hdr.segment_id;

	/* Ensure the segment is sized for this data */
	data = stk_sequence_iterator_ensure_segment_size(readbuf->seqiter,readbuf->segment_hdr.segment_len);

	if(!data) {
		/* Failed to ensure its the required size, fail! */
		stk_tcp_server_t *ts = stk_data_flow_module_data(df);
		STK_LOG(STK_LOG_ERROR,"data buffer in sequence couldn't be resized for data read %d. TCP fd %d for data flow %s[%lu], env %p",
			readbuf->segment_hdr.segment_len,ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df));
		return STK_MEMERR;
	}

	/* copy in to buffer */
	rc = stk_tcp_fill_segment(data,df,readbuf);
	if(rc != STK_SUCCESS || readbuf->cb_rc != STK_SUCCESS) return rc;

	readbuf->blks_rcvd++;

	STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"set the size of the buffer %ld to the size of the received data %d",
		stk_sequence_iterator_alloc_size(readbuf->seqiter), readbuf->segment_hdr.segment_len);

	return STK_SUCCESS;
}

stk_sequence_t *stk_tcp_server_data_flow_rcv_internal(stk_data_flow_t *df,stk_tcp_server_t *ts,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	stk_ret rc;
	stk_tcp_wire_basic_hdr_t bhdr;
	stk_uint64 bytes_read = 0;

	STK_DEBUG_BUFFER("enter flow_recv",&ts->readbuf);
	if(ts->readbuf.elem_start == ts->readbuf.read) {
		/* Nothing preread - reset to start of buffer - this reduces the wrap around overhead */
		ts->readbuf.elem_start = 0;
		ts->readbuf.read = 0;
	}
	ts->readbuf.cb_rc = STK_SUCCESS;

	STK_TCP_PARSE_START(&ts->readbuf);
	while(stk_tcp_data_buffered(&ts->readbuf) < sizeof(bhdr)) {
		if(ts->readbuf.read == ts->readbuf.sz && (ts->readbuf.sz - ts->readbuf.orig_elem_start > 0))
			STK_ASSERT(STKA_NET,stk_tcp_shift_buf(&ts->readbuf)==STK_SUCCESS,"shift readbuf down %p %lu",&ts->readbuf,ts->readbuf.elem_start);

		bytes_read = stk_tcp_server_recv(df,&ts->readbuf);
#ifdef ZEROREAD_RETURNS
		if(bytes_read == 0)
			return NULL;
#endif
		ts->readbuf.read += bytes_read;
	}
	/* Save data out of read buffer for persistent usage while reading other data */
	memcpy(&bhdr,&ts->readbuf.buf[ts->readbuf.elem_start],sizeof(bhdr));
	ts->readbuf.elem_start += sizeof(bhdr);

	STK_ASSERT(STKA_NET,bhdr.type!=STK_SEQUENCE_TYPE_INVALID,"is new hdr type valid %d",bhdr.type);
	STK_DEBUG(STKA_NET,"bhdr version %d compat %d flags %x id %lu type %d",bhdr.wire_version,bhdr.wire_compat,bhdr.flags,bhdr.id,bhdr.type);

	/* Update the sequence with the type and ID from the wire */
	rc = stk_set_sequence_type(data_sequence,bhdr.type);
	if(rc != STK_SUCCESS) {
		STK_LOG(STK_LOG_ERROR,"update the sequence type for a sequence from tcp fd %d for data flow %s[%lu], env %p rc %d",
			ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
		return NULL;
	}

	/* FIXME - is generation being set?? */
	rc = stk_set_sequence_id(data_sequence,bhdr.id);
	if(rc != STK_SUCCESS) {
		STK_LOG(STK_LOG_ERROR,"update the sequence id for a sequence from tcp fd %d for data flow %s[%lu], env %p rc %d",
			ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
		return NULL;
	}

	rc = stk_data_flow_add_client_ip(data_sequence,&ts->accept_addr,sizeof(ts->accept_addr));
	if(rc != STK_SUCCESS) {
		STK_LOG(STK_LOG_ERROR,"update the client IP for a sequence from tcp fd %d for data flow %s[%lu], env %p rc %d",
			ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
		return NULL;
	}

	rc = stk_data_flow_add_client_protocol(data_sequence,stk_tcp_server_data_flow_protocol(df));
	if(rc != STK_SUCCESS) {
		STK_LOG(STK_LOG_ERROR,"update the client protocol for a sequence from tcp fd %d for data flow %s[%lu], env %p rc %d",
			ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
		return NULL;
	}

	if(bhdr.flags & STK_TCP_FLAG_NAME_FOLLOWS) {
		/* Read in to temporary buffer */
		stk_uint16 slen;

		while(stk_tcp_data_buffered(&ts->readbuf) < sizeof(slen)) {
			if(ts->readbuf.read == ts->readbuf.sz)
				STK_ASSERT(STKA_NET,stk_tcp_shift_buf(&ts->readbuf)==STK_SUCCESS,"shift readbuf down %p %lu",&ts->readbuf,ts->readbuf.elem_start);
			bytes_read = stk_tcp_server_recv(df,&ts->readbuf);
#ifdef ZEROREAD_RETURNS
			if(bytes_read == 0)
				return NULL;
#endif
			ts->readbuf.read += bytes_read;
		}
		memcpy(&slen,&ts->readbuf.buf[ts->readbuf.elem_start],sizeof(slen));
		ts->readbuf.elem_start += sizeof(slen);

		/* Now read the string */
		while(stk_tcp_data_buffered(&ts->readbuf) < slen) {
			if(ts->readbuf.read == ts->readbuf.sz)
				STK_ASSERT(STKA_NET,stk_tcp_shift_buf(&ts->readbuf)==STK_SUCCESS,"shift readbuf down %p %lu",&ts->readbuf,ts->readbuf.elem_start);
			bytes_read = stk_tcp_server_recv(df,&ts->readbuf);
#ifdef ZEROREAD_RETURNS
			if(bytes_read == 0)
				return NULL;
#endif
			ts->readbuf.read += bytes_read;
		}

		{
		char *str = (char *) malloc(slen);
		memcpy(str,&ts->readbuf.buf[ts->readbuf.elem_start],slen);
		ts->readbuf.elem_start += slen;

		STK_DEBUG(STKA_NET,"sqn name %s",str);

		/* Sequence Name is in the next element */
		rc = stk_set_sequence_name(data_sequence,str);
		if(rc != STK_SUCCESS) {
			STK_LOG(STK_LOG_ERROR,"update the sequence name for a sequence from tcp fd %d for data flow %s[%lu], env %p rc %d",
				ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
			return NULL;
		}
		}
	}

	if(bhdr.flags & STK_TCP_FLAG_SEGMENTS_FOLLOW) {
		ts->readbuf.blks_rcvd = 0;

		/* Read in to buffers */
		ts->readbuf.seqiter = stk_sequence_iterator(data_sequence);
		if(!ts->readbuf.seqiter)
			return NULL;

		/* Iterate over the sequence and collect the segments */
		rc = stk_iterate_sequence((stk_sequence_t *) ts->readbuf.seqiter,stk_tcp_server_segment_rcv_cb,&ts->readbuf);
		if(rc != STK_SUCCESS) {
			STK_LOG(STK_LOG_ERROR,"iterate reading data for a sequence from tcp fd %d for data flow %s[%lu], env %p rc %d",
				ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
			stk_end_sequence_iterator(ts->readbuf.seqiter);
			return NULL;
		}

		stk_end_sequence_iterator(ts->readbuf.seqiter);

		/*
		 * The segment_hdr.nblks will be 0 if the sequence has no preallocated elements,
		 * so iterate_sequence above didn't read the first segment to know how many there are.
		 */
		while(ts->readbuf.blks_rcvd < ts->readbuf.segment_hdr.nblks || ts->readbuf.segment_hdr.nblks == 0) {
			/* More to read, append data to sequence! */

			/* Read the segment header, then the data */
			rc = stk_tcp_read_segment_hdr(df,&ts->readbuf);
			if(rc != STK_SUCCESS || ts->readbuf.cb_rc != STK_SUCCESS) return NULL;

			STK_DEBUG(STKA_NET,"sqn more segments len %d user type %lx",ts->readbuf.segment_hdr.segment_len,ts->readbuf.segment_hdr.user_type);

			/* Append a new block to the sequence, and fill */
			rc = stk_alloc_in_sequence(data_sequence,ts->readbuf.segment_hdr.segment_len,ts->readbuf.segment_hdr.user_type);
			if(rc != STK_SUCCESS) return NULL;

			rc = stk_tcp_fill_segment(stk_last_sequence_element(data_sequence),df,&ts->readbuf);
			if(rc != STK_SUCCESS || ts->readbuf.cb_rc != STK_SUCCESS) return NULL;

			ts->readbuf.blks_rcvd++;
		}
	}

	if(ts->readbuf.cb_rc != STK_SUCCESS) {
		STK_LOG(STK_LOG_ERROR,"read all the data for a sequence from tcp fd %d for data flow %s[%lu], env %p rc %d",
			ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),ts->readbuf.cb_rc);
		return NULL;
	}
	STK_DEBUG_BUFFER("exit flow_recv",&ts->readbuf);
	return data_sequence;
}

stk_sequence_t *stk_tcp_server_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	stk_tcp_server_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	stk_sequence_t *seq = stk_tcp_server_data_flow_rcv_internal(df,ts,data_sequence,flags);
	if(seq == NULL)
		STK_TCP_PARSE_RESET(&ts->readbuf);
	return seq;
}

#define STK_SET_IOV(_vector,_ptr,_len) do { \
	(_vector)->iov_base = _ptr; \
	(_vector)->iov_len = (size_t) _len; \
	} while(0)

typedef struct stk_tcp_vector_cb_stct {
	stk_uint16 segment_id;
	struct iovec *vptr;
	stk_uint8 nblks;
	stk_uint8 blk_num;
} stk_tcp_vector_cb_t;

stk_ret stk_tcp_server_vector_cb(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	stk_tcp_vector_cb_t *vcb = (stk_tcp_vector_cb_t *) clientd;
	stk_tcp_wire_seqment_hdr_t *sgmt;

	sgmt = calloc(1,sizeof(stk_tcp_wire_seqment_hdr_t));
	sgmt->segment_id = vcb->segment_id++;
	/* TODO: create multiple vectors if the element size is larger than a segment */
	sgmt->nblks = vcb->nblks;
	sgmt->blk_num = vcb->blk_num++;
	sgmt->segment_len = sz;
	sgmt->user_type = user_type;

	STK_DEBUG(STKA_NET,"setting vectors sgmt hdr %lu sgmt %lu",sizeof(stk_tcp_wire_seqment_hdr_t),sz);
	STK_SET_IOV(vcb->vptr,sgmt,sizeof(stk_tcp_wire_seqment_hdr_t));
	vcb->vptr++;
	STK_SET_IOV(vcb->vptr,data,sz);
	vcb->vptr++;
	return STK_SUCCESS;
}

stk_ret stk_tcp_server_data_flow_send(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	stk_tcp_server_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	stk_tcp_wire_basic_hdr_t bhdr;
	struct iovec *vectors,*vptr;
	char *seq_name = stk_get_sequence_name(data_sequence);
	int num_elements = stk_number_of_sequence_elements(data_sequence);
	int num_chunks = (num_elements * 2 /* segment hdr + data */) + (seq_name ? 2 : 1);
	int start_idx = 1;
	char *allocname = NULL;

	if(ts->sock == -1) return STK_WOULDBLOCK; /* May happen if a connection from a tcp client reset and is in the process of reconnecting */

	STK_ASSERT(STKA_NET,num_chunks<STK_MAX_IOV,"number of chunks for scatter gather send on this O/S (%d %d)",num_chunks,STK_MAX_IOV);

	vectors = calloc(num_chunks, sizeof(struct iovec));
	STK_ASSERT(STKA_NET,vectors!=NULL,"allocate a vector for %d chunks",num_chunks);

	/* After the first vector, they follow the pattern of header, data
	 * with headers dynamically allocated
	 */
	STK_TCP_INIT_BASIC_HDR(&bhdr,data_sequence,
		(num_elements > 0 ? STK_TCP_FLAG_SEGMENTS_FOLLOW : 0) | (seq_name ? STK_TCP_FLAG_NAME_FOLLOWS : 0));
	STK_SET_IOV(&vectors[0],&bhdr,sizeof(bhdr));


	if(num_elements > 0 || bhdr.flags & STK_TCP_FLAG_NAME_FOLLOWS)
	{
		stk_tcp_vector_cb_t vcb;
		int idx = 1;
		stk_ret rc;

		if(bhdr.flags & STK_TCP_FLAG_NAME_FOLLOWS) {
			int slen = strlen(seq_name) + 1;
			allocname = malloc(slen + sizeof(stk_uint16));
			STK_ASSERT(STKA_NET,allocname!=NULL,"allocate wire name for sequence to send");

			*((stk_uint16*)allocname) = (stk_uint16) slen;
			strcpy(&allocname[sizeof(stk_uint16)],seq_name);

			STK_SET_IOV(&vectors[start_idx],allocname,slen + sizeof(stk_uint16));
			start_idx++;
		}

		if(num_elements > 0) {
			if((flags & STK_TCP_SEND_FLAG_REUSE_GENID) == 0)
				stk_bump_sequence_generation(data_sequence);

			vcb.vptr = &vectors[start_idx];

			vcb.segment_id = 0;
			vcb.nblks = num_elements;
			vcb.blk_num = 0;

			rc = stk_iterate_sequence(data_sequence,stk_tcp_server_vector_cb,&vcb);
			if(rc != STK_SUCCESS) {
				stk_tcp_server_t *ts = stk_data_flow_module_data(df);
				STK_LOG(STK_LOG_ERROR,"iterate reading data for a sequence from tcp fd %d for data flow %s[%lu], env %p rc %d",
					ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);

				/* Free vector segments and vector */
				for(int idx = start_idx; idx < num_chunks; idx+=2)
					free(vectors[idx].iov_base);
				free(vectors);
				if(allocname) free(allocname);
				return rc;
			}
		}
	}

	{
	stk_ret rc = stk_send_vector(df,vectors,num_chunks,start_idx,flags);

	if(allocname) free(allocname);

	return rc;
	}
}

stk_ret stk_send_vector(stk_data_flow_t *df,struct iovec *vectors,int num_chunks,int free_start_idx,stk_uint64 flags)
{
	stk_tcp_server_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	ssize_t sendsz = 0,sentsz;
	struct msghdr msg;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = vectors;
	msg.msg_iovlen = num_chunks;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	for(int idx = 0; idx < num_chunks; idx++) {
		sendsz += vectors[idx].iov_len;
		STK_DEBUG(STKA_NET,"vector base %p size %lu",vectors[idx].iov_base,vectors[idx].iov_len);
	}

	if(flags & STK_TCP_SEND_FLAG_NONBLOCK)
		sentsz = sendmsg(ts->sock, &msg, STK_NB_SEND_FLAGS);
	else {
		do {
			sentsz = sendmsg(ts->sock, &msg, STK_NB_SEND_FLAGS);
		} while(sentsz == -1 && errno == EWOULDBLOCK);
	}
	
	STK_DEBUG(STKA_NET,"df %p fd %d sendsz %lu sentsz %lu",df,ts->sock,sendsz,sentsz);
	if(sendsz != sentsz && sentsz != -1) {
		/* Not all the data was sent. Alloc a new vector table to send remaining */
		int unsent_offset_for_iov = 0;
		struct iovec *new_vectors;

		/* Find the first vector index not fully sent */
		int resend_start_idx = 0;
		ssize_t countsz = 0;
		for(; (ssize_t) vectors[resend_start_idx].iov_len + countsz <= sentsz; resend_start_idx++)
			countsz += vectors[resend_start_idx].iov_len;

		STK_ASSERT(STKA_NET,resend_start_idx < num_chunks,"resend index should not exceed end of vectors");

		/* Now determine how much of the first partially sent vector was sent (if any) */
		unsent_offset_for_iov = sentsz - countsz;

		/* Alloc a new vector from resend_start_idx to the end */
		new_vectors = calloc(num_chunks - resend_start_idx, sizeof(struct iovec));
		STK_ASSERT(STKA_NET,new_vectors!=NULL,"allocate a vector for %d chunks to be resent",num_chunks - resend_start_idx);

		/* Now set the first vector to the first unsent byte in the first vector, and iterate copying all the other vector pointers */
		STK_SET_IOV(&new_vectors[0],(char *) vectors[resend_start_idx].iov_base + unsent_offset_for_iov, vectors[resend_start_idx].iov_len - unsent_offset_for_iov);
		for(int idx = 1; idx < num_chunks - resend_start_idx; idx++)
			STK_SET_IOV(&new_vectors[idx],vectors[resend_start_idx + idx].iov_base,vectors[resend_start_idx + idx].iov_len);

		/* Send the new vector containing the remainder of the data!! */
		{
		/* This converts a non blocking send in to a blocking send... Bad... But necessary because we don't track partial sends... To be improved... */
		stk_ret rc = stk_send_vector(df,new_vectors,num_chunks - resend_start_idx,num_chunks /* set greater than size to avoid frees */,flags & ~STK_TCP_SEND_FLAG_NONBLOCK);
		STK_DEBUG(STKA_NET,"resending unsent data, rc %d",rc);
		if(rc != STK_SUCCESS) return rc;
		}
	}

	/* Free vector segments and vector */
	for(int idx = free_start_idx; idx < num_chunks; idx+=2)
		free(vectors[idx].iov_base);
	free(vectors);

	if(sentsz == -1) {
		int rc;

		switch(errno) {
		case EWOULDBLOCK: rc = STK_WOULDBLOCK; break;
		case EPIPE: rc = STK_RESET; break;
		default: rc = STK_SYSERR; break;
		}

		if(rc != STK_WOULDBLOCK)
			STK_LOG(STK_LOG_NET_ERROR,"Send failed on tcp fd %d for data flow '%s[%lu]', env %p errno %d",
				ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),errno);

		return rc;
	}

	return STK_SUCCESS;
}

stk_ret stk_tcp_server_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	stk_tcp_server_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	memcpy(data_flow_id,&ts->server_addr,addrlen);
	return STK_SUCCESS;
}

stk_ret stk_tcp_server_data_flow_clientip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	stk_tcp_server_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	memcpy(data_flow_id,&ts->client_addr,addrlen);
	return STK_SUCCESS;
}

stk_ret stk_tcp_shift_buf(stk_tcp_wire_read_buf_t *readbuf)
{
	if(readbuf->orig_elem_start == 0) return ~STK_SUCCESS;

	memcpy(&readbuf->buf[0],&readbuf->buf[readbuf->orig_elem_start],readbuf->sz - readbuf->orig_elem_start);
	readbuf->read -= readbuf->orig_elem_start;
	readbuf->elem_start -= readbuf->orig_elem_start;
	readbuf->orig_elem_start = 0;

	return STK_SUCCESS;
}

size_t stk_tcp_data_buffered(stk_tcp_wire_read_buf_t *readbuf)
{
	STK_DEBUG_BUFFER("buffered",readbuf);
	return readbuf->read - readbuf->elem_start;
}

stk_ret stk_tcp_server_data_flow_buffered(stk_data_flow_t *df)
{
	stk_tcp_server_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	return stk_tcp_data_buffered(&ts->readbuf) > 0 ? STK_SUCCESS : !STK_SUCCESS;
}

char *stk_tcp_server_data_flow_protocol(stk_data_flow_t *df) { return "tcp"; }

