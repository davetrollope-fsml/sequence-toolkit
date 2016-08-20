#include "stk_data_flow_api.h"
#include "stk_data_flow.h"
#include "stk_common.h"
#include "stk_internal.h"
#include "stk_env.h"
#include "stk_sequence.h"
#include "stk_sequence_api.h"
#include "stk_rawudp_api.h"
#include "stk_udp.h"
#include "stk_ports.h"
#include "stk_options_api.h"
#include "stk_env_api.h"
#include "stk_timer_api.h"
#include "stk_sync_api.h"
#include "stk_udp.h"


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

#define STK_MAX_IOV IOV_MAX
#ifdef __APPLE__
#define STK_NB_SEND_FLAGS 0
#else
#define STK_NB_SEND_FLAGS MSG_NOSIGNAL
#endif

stk_ret stk_rawudp_listener_destroy_data_flow(stk_data_flow_t *flow);
stk_sequence_t *stk_rawudp_listener_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);
stk_ret stk_rawudp_listener_data_flow_buffered(stk_data_flow_t *flow);
char *stk_rawudp_data_flow_protocol(stk_data_flow_t *flow);
stk_ret stk_rawudp_client_data_flow_sendbuf(stk_data_flow_t *df,char *buf,stk_uint64 buflen,stk_uint64 flags);
stk_ret stk_rawudp_listener_data_flow_send(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);

static stk_data_flow_module_t rawudp_listener_fptrs = {
	stk_rawudp_listener_create_data_flow, stk_rawudp_listener_destroy_data_flow,
	stk_rawudp_listener_data_flow_send, stk_rawudp_listener_data_flow_rcv,
	stk_rawudp_listener_data_flow_id_ip, stk_rawudp_listener_data_flow_buffered,
	stk_rawudp_data_flow_protocol
};

typedef struct stk_rawudp_listener_stct {
	int sock;
	short port;
	struct sockaddr_in client_addr;
	struct sockaddr_in server_addr;
	stk_data_flow_fd_created_cb fd_created_cb;
	stk_data_flow_fd_destroyed_cb fd_destroyed_cb;
	stk_data_flow_t *cb_df;
	stk_udp_wire_read_buf_t readbuf;
	stk_sequence_id seq_id;
	stk_sequence_type seq_type;
	char *seq_name;
	stk_uint64 seq_user_type;
	char *mcast_str;
	struct in_addr mcast_addr;
} stk_rawudp_listener_t;

extern void stk_dump_hex(unsigned char *ptr, ssize_t ret, int offset);

stk_timer_set_t *stk_rawudp_client_timers;
static int timer_refcount;

stk_ret stk_rawudp_client_destroy_data_flow(stk_data_flow_t *flow);
stk_ret stk_rawudp_client_data_flow_send(stk_data_flow_t *flow,stk_sequence_t *data_sequence,stk_uint64 flags);
stk_ret stk_rawudp_client_data_flow_buffered(stk_data_flow_t *df);
stk_ret stk_rawudp_listener_data_flow_send(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);
stk_ret stk_rawudp_listener_data_flow_sendbuf(stk_data_flow_t *df,char *buf,stk_uint64 buflen,stk_uint64 flags);

static stk_data_flow_module_t rawudp_client_fptrs = {
	stk_rawudp_client_create_data_flow, stk_rawudp_client_destroy_data_flow,
	stk_rawudp_client_data_flow_send, NULL,
	stk_rawudp_client_data_flow_id_ip, stk_rawudp_client_data_flow_buffered,
	stk_rawudp_data_flow_protocol
};

typedef struct stk_rawudp_client_stct {
	int sock;
	short port;
	struct sockaddr_in server_addr;
	struct sockaddr_in client_addr;
	stk_data_flow_fd_created_cb fd_created_cb;
	stk_data_flow_fd_destroyed_cb fd_destroyed_cb;
	stk_data_flow_t *cb_df;
} stk_rawudp_client_t;


stk_data_flow_t *stk_rawudp_listener_create_data_flow(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options)
{
	stk_data_flow_t *df = stk_alloc_data_flow(env,STK_RAWUDP_LISTENER_FLOW,name,id,sizeof(stk_rawudp_listener_t),&rawudp_listener_fptrs,options);

	if(df) {
		stk_rawudp_listener_t *ts = stk_data_flow_module_data(df);
		short port = STK_DEFAULT_RAWUDP_LISTENER_PORT;
		int rc;

		ts->cb_df = df;

		ts->sock = socket(PF_INET, SOCK_DGRAM, 0);
		if(ts->sock == -1) {
			STK_LOG(STK_LOG_ERROR,"create listener socket for data flow '%s'[%lu], env %p",name,id,env);
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
			STK_LOG(STK_LOG_ERROR,"Failed to set listener socket no sigpipe on port %d, env %p",port,env);
		}
#endif

		{
		void *mcastaddr_str = stk_find_option(options,"multicast_address",NULL);
		void *bindaddr_str = stk_find_option(options,"bind_address",NULL);
		void *reuseaddr_str = stk_find_option(options,"reuseaddr",NULL);
		void *port_str = stk_find_option(options,"bind_port",NULL);
		void *sndbuf_str = stk_find_option(options,"send_buffer_size",NULL);
		void *rcvbuf_str = stk_find_option(options,"receive_buffer_size",NULL);
		void *seqname_str = stk_find_option(options,"sequence_name",NULL);
		void *seqtype = stk_find_option(options,"sequence_type",NULL);
		void *sequsertype = stk_find_option(options,"sequence_user_type",NULL);
		void *seqid = stk_find_option(options,"sequence_id",NULL);
		void *destaddr_str = stk_find_option(options,"destination_address",NULL);
		void *destport_str = stk_find_option(options,"destination_port",NULL);
		void *cb_df = stk_find_option(options,"callback_data_flow",NULL);

		/* Set the callers data flow to be used in callbacks */
		if(cb_df)
			ts->cb_df = cb_df;

		/* Need to set the following from opts 
			stk_sequence_id seq_id;
			stk_uint64 seq_user_type;
		 */
		ts->seq_name = strdup(seqname_str ? seqname_str : "UDP");
		ts->seq_type = (seqtype ? (stk_sequence_type) seqtype : STK_SEQUENCE_TYPE_DATA);
		if(seqid) ts->seq_id = (stk_sequence_id) seqid;
		if(sequsertype) ts->seq_user_type = (stk_uint64) sequsertype;

		ts->server_addr.sin_family = AF_INET;
		ts->fd_created_cb = (stk_data_flow_fd_created_cb) stk_find_option(options,"fd_created_cb",NULL);
		ts->fd_destroyed_cb = (stk_data_flow_fd_destroyed_cb) stk_find_option(options,"fd_destroyed_cb",NULL);

		if(port_str) {
			port = (short) atoi(port_str);
			ts->server_addr.sin_port = htons(port);
		}

		if(destport_str) {
			port = (short) atoi(destport_str);
			ts->client_addr.sin_port = htons(port);
		}

		ts->client_addr.sin_family = AF_INET;
		if(destaddr_str) {
			rc = inet_pton(AF_INET,destaddr_str,&ts->client_addr.sin_addr);
			if(rc <= 0) {
				STK_LOG(STK_LOG_ERROR,"Could not convert destination address %s\n",destaddr_str);
				ts->client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
			}
		} else
			ts->client_addr.sin_addr.s_addr = htonl(INADDR_ANY);

		if(reuseaddr_str) {
			int true = 1;
			rc = setsockopt(ts->sock, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(true));
			if(rc < 0)
				STK_LOG(STK_LOG_ERROR,"Failed to set listener socket to REUSEADDR on port %d, env %p",port,env);
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

		if(bindaddr_str) {
			rc = inet_pton(AF_INET,bindaddr_str,&ts->server_addr.sin_addr);
			if(rc <= 0) {
				STK_LOG(STK_LOG_ERROR,"Could not convert bind address errno %d",errno);
				ts->server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
			}
		} else
			ts->server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

		if(bind(ts->sock,(struct sockaddr *) &ts->server_addr,sizeof(ts->server_addr)) == -1) {
			close(ts->sock);
			STK_LOG(STK_LOG_ERROR,"Failed to bind listener socket for data flow '%s'[%lu] to port %d, errno %d %s : env %p",name,id,port,errno,strerror(errno),env);
			stk_free_data_flow(df);
			return NULL;
		}
		if(mcastaddr_str) {
			ts->mcast_str = mcastaddr_str;

			/* In the future, this could handle multiple addresses... */
			struct ip_mreq mcast_addr_req;

			memset(&mcast_addr_req,0,sizeof(mcast_addr_req));

			rc = inet_pton(AF_INET,mcastaddr_str,&ts->mcast_addr);
			if(rc <= 0) {
				STK_LOG(STK_LOG_ERROR,"Could not convert multicast address errno %d",errno);
				ts->server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
			}
			memcpy(&mcast_addr_req.imr_multiaddr,&ts->mcast_addr,sizeof(mcast_addr_req.imr_multiaddr));
			mcast_addr_req.imr_interface = ts->server_addr.sin_addr;

			{
			unsigned char loop = 1;
			if(setsockopt(ts->sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop))) {
				STK_LOG(STK_LOG_ERROR,"Failed to set multicast loopback for data flow '%s'[%lu] to port %d, errno %d %s : env %p",name,id,port,errno,strerror(errno),env);
				close(ts->sock);
				stk_free_data_flow(df);
				return NULL;
			}
			}

			rc = setsockopt(ts->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mcast_addr_req, sizeof(mcast_addr_req));
			if(rc < 0) {
				STK_LOG(STK_LOG_ERROR,"Failed to set listener socket to multicast on addr %s, env %p errno %d",mcastaddr_str,env,errno);
				close(ts->sock);
				stk_free_data_flow(df);
				return NULL;
			}
		}
		}

		STK_LOG(STK_LOG_NORMAL,"data flow %p %s[%lu] to port %d created (fd %d)",df,stk_data_flow_name(df),stk_get_data_flow_id(df),ntohs(ts->client_addr.sin_port),ts->sock);

		if(ts->fd_created_cb)
			ts->fd_created_cb(ts->cb_df,stk_get_data_flow_id(ts->cb_df),ts->sock);

		return df;
	}

	return NULL;
}

stk_ret stk_rawudp_listener_destroy_data_flow(stk_data_flow_t *df)
{
	stk_rawudp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */

	if(ts->seq_name) free(ts->seq_name);

	if(ts->fd_destroyed_cb)
		ts->fd_destroyed_cb(df,stk_get_data_flow_id(df),ts->sock);

	if(ts->sock) close(ts->sock);

	return stk_free_data_flow(df);
}

int stk_rawudp_listener_fd(stk_data_flow_t *svr_df)
{
	stk_rawudp_listener_t *ts = stk_data_flow_module_data(svr_df); /* Asserts on structure type */
	return ts->sock;
}

stk_ret stk_rawudp_listener_data_flow_send_dest(stk_data_flow_t *df,char *buf,stk_uint64 buflen,stk_uint64 flags,
	struct sockaddr_in *dest_addr,size_t sz)
{
	stk_rawudp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	ssize_t sendsz = 0,sentsz;

	if(flags & STK_UDP_SEND_FLAG_NONBLOCK) {
		sentsz = sendto(ts->sock, buf, (int)buflen, STK_NB_SEND_FLAGS, (struct sockaddr *) dest_addr, sz);
	} else {
		do {
			sentsz = sendto(ts->sock, buf, (int)buflen, STK_NB_SEND_FLAGS, (struct sockaddr *) dest_addr, sz);
		} while(sentsz == -1 && errno == EWOULDBLOCK);
	}
	
	STK_DEBUG(STKA_NET,"df %p fd %d sendsz %lu sentsz %lu",df,ts->sock,sendsz,sentsz);

	if(sentsz == -1) {
		stk_ret rc;

		switch(errno) {
		case EWOULDBLOCK: rc = STK_WOULDBLOCK; break;
		case EPIPE: rc = STK_RESET; break;
		default: rc = STK_SYSERR; break;
		}

		if(rc != STK_WOULDBLOCK)
			STK_LOG(STK_LOG_NET_ERROR,"Send failed on rawudp fd %d for data flow '%s[%lu]' size %lu, env %p errno %d",
				ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),buflen,stk_env_from_data_flow(df),errno);

		return rc;
	}
	return STK_SUCCESS;
}

stk_ret stk_rawudp_listener_data_flow_sendbuf(stk_data_flow_t *df,char *buf,stk_uint64 buflen,stk_uint64 flags)
{
	stk_rawudp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	return stk_rawudp_listener_data_flow_send_dest(df,buf,buflen,flags,&ts->client_addr,sizeof(ts->client_addr));
}

stk_ret stk_rawudp_listener_data_flow_send(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	stk_rawudp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	stk_sequence_iterator_t *seq_iter;
	char *buf;
	stk_uint64 buflen;
	stk_ret rc;

	STK_API_DEBUG();
	if(ts->sock == -1) return !STK_SUCCESS;

	if(stk_number_of_sequence_elements(data_sequence) > 1)
		return STK_DATA_TOO_LARGE;

	/* Get first elem from sequence */
	seq_iter = stk_sequence_iterator(data_sequence);
	STK_ASSERT(STKA_NET,seq_iter!=NULL,"create iterator to send rawudp fd %d for data flow %p",ts->sock,df);

	buf = stk_sequence_iterator_data(seq_iter);
	if(!buf) return STK_SUCCESS; /* No data to send */

	buflen = stk_sequence_iterator_data_size(seq_iter);

	rc = stk_end_sequence_iterator(seq_iter);
	STK_DEBUG(STKA_NET,"end iterator %d",rc);

	if((flags & STK_UDP_SEND_FLAG_REUSE_GENID) == 0)
		stk_bump_sequence_generation(data_sequence);

	rc = stk_rawudp_listener_data_flow_sendbuf(df,buf,buflen,flags);
	return rc;
}

stk_uint64 stk_rawudp_listener_recv(stk_data_flow_t *df,stk_udp_wire_read_buf_t *bufread)
{
	stk_rawudp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	ssize_t ret;

	bufread->from_address_len = sizeof(bufread->from_address);

	stk_set_data_flow_errno(df,0);
	ret = recvfrom(ts->sock,bufread->buf,sizeof(bufread->buf),0,(struct sockaddr *) &bufread->from_address,&bufread->from_address_len);

	STK_DEBUG(STKA_NET,"recv df %p fd %d ret %ld errno %d",df,ts->sock,ret,errno);
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
		stk_dump_hex((unsigned char *) bufread->buf,ret,0);

	bufread->read = ret;
	return (stk_uint64) ret;
}

stk_sequence_t *stk_rawudp_listener_data_flow_rcv_internal(stk_data_flow_t *df,stk_rawudp_listener_t *ts,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	stk_ret rc;

	stk_rawudp_listener_recv(df,&ts->readbuf);

	/* Update the sequence with the type and ID from the wire */
	rc = stk_set_sequence_type(data_sequence,ts->seq_type);
	if(rc != STK_SUCCESS) {
		STK_LOG(STK_LOG_ERROR,"update the sequence type for a sequence from rawudp fd %d for data flow %s[%lu], env %p rc %d",
			ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
		return NULL;
	}
	rc = stk_set_sequence_id(data_sequence,ts->seq_id);
	if(rc != STK_SUCCESS) {
		STK_LOG(STK_LOG_ERROR,"update the sequence id for a sequence from rawudp fd %d for data flow %s[%lu], env %p rc %d",
			ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
		return NULL;
	}
	/* Sequence Name is in the next element */
	rc = stk_set_sequence_name(data_sequence,strdup(ts->seq_name));
	if(rc != STK_SUCCESS) {
		STK_LOG(STK_LOG_ERROR,"update the sequence name for a sequence from rawudp fd %d for data flow %s[%lu], env %p rc %d",
			ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
		return NULL;
	}

	rc = stk_rawudp_listener_add_client_ip(df,data_sequence,&ts->readbuf);
	if(rc != STK_SUCCESS) {
		STK_LOG(STK_LOG_ERROR,"update the client IP for a sequence from rawudp fd %d for data flow %s[%lu], env %p rc %d",
			ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
		return NULL;
	}

	rc = stk_data_flow_add_client_protocol(data_sequence,stk_rawudp_data_flow_protocol(df));
	if(rc != STK_SUCCESS) {
		STK_LOG(STK_LOG_ERROR,"update the client protocol for a sequence from rawudp fd %d for data flow %s[%lu], env %p rc %d",
			stk_rawudp_listener_fd(df),stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
		return NULL;
	}

	if(stk_number_of_sequence_elements(data_sequence) == 0) {
		/* Add received data to seq */
		rc = stk_copy_to_sequence(data_sequence,ts->readbuf.buf,ts->readbuf.read,ts->seq_user_type);
		if(rc != STK_SUCCESS) {
			STK_LOG(STK_LOG_ERROR,"copy received data to sequence from rawudp fd %d for data flow %s[%lu], env %p rc %d",
				ts->sock,stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
			return NULL;
		}
	} else {
		/* Fill in to existing segment */
		stk_sequence_iterator_t *seqiter = stk_sequence_iterator(data_sequence);
		void *data = stk_sequence_iterator_ensure_segment_size(seqiter,ts->readbuf.read);
		memcpy(data,ts->readbuf.buf,ts->readbuf.read);
		rc = stk_end_sequence_iterator(seqiter);
		STK_ASSERT(STKA_NET,rc == STK_SUCCESS,"end sequence iterator rc %d",rc);
	}

	return data_sequence;
}

stk_sequence_t *stk_rawudp_listener_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	stk_rawudp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	stk_sequence_t *seq = stk_rawudp_listener_data_flow_rcv_internal(df,ts,data_sequence,flags);
	return seq;
}

stk_ret stk_rawudp_listener_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	stk_rawudp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	if(ts->mcast_str) {
		struct sockaddr_in mcast_addr;
		memcpy(&mcast_addr,&ts->server_addr,sizeof(mcast_addr));
		memcpy(&mcast_addr.sin_addr,&ts->mcast_addr,sizeof(ts->mcast_addr));
		memcpy(data_flow_id,&mcast_addr,addrlen);
	} else
		memcpy(data_flow_id,&ts->server_addr,addrlen);
	return STK_SUCCESS;
}

stk_ret stk_rawudp_listener_data_flow_clientip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	stk_rawudp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	memcpy(data_flow_id,&ts->client_addr,addrlen);
	return STK_SUCCESS;
}

stk_ret stk_rawudp_listener_data_flow_buffered(stk_data_flow_t *df)
{
	stk_rawudp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	return !STK_SUCCESS;
}

stk_ret stk_rawudp_listener_add_client_ip(stk_data_flow_t *df,stk_sequence_t *seq,stk_udp_wire_read_buf_t *bufread)
{
	return stk_data_flow_add_client_ip(seq,&bufread->from_address,sizeof(bufread->from_address));
}

char *stk_rawudp_data_flow_protocol(stk_data_flow_t *df) { return "rawudp"; }

stk_data_flow_t *stk_rawudp_client_create_data_flow(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options)
{
	stk_data_flow_t *df = stk_alloc_data_flow(env,STK_RAWUDP_CLIENT_FLOW,name,id,sizeof(stk_rawudp_client_t),&rawudp_client_fptrs,options);
	stk_rawudp_client_t *ts = df ? stk_data_flow_module_data(df) : NULL;
	int rc;

	STK_API_DEBUG();

	if(stk_rawudp_client_timers == NULL) {
		stk_rawudp_client_timers = stk_new_timer_set(env,NULL,0,STK_TRUE);
		STK_ASSERT(STKA_NET,stk_rawudp_client_timers!=NULL,"allocate a timer set for RAWUDP clients");
	}

	if(df) {
		STK_ATOMIC_INCR(&timer_refcount);

		short port = 29090;
		void *destaddr_str = stk_find_option(options,"destination_address",NULL);
		void *port_str = stk_find_option(options,"destination_port",NULL);
		void *mcastintf_str = stk_find_option(options,"multicast_interface",NULL);
		void *sndbuf_str = stk_find_option(options,"send_buffer_size",NULL);
		void *cb_df = stk_find_option(options,"callback_data_flow",NULL);
		int sndbuf = 1048576; /* Default to 1MB Send buf */

		/* Set the callers data flow to be used in callbacks */
		if(cb_df)
			ts->cb_df = cb_df;
		else
			ts->cb_df = df;

		ts->sock = -1;

		ts->fd_created_cb = (stk_data_flow_fd_created_cb) stk_find_option(options,"fd_created_cb",NULL);
		ts->fd_destroyed_cb = (stk_data_flow_fd_destroyed_cb) stk_find_option(options,"fd_destroyed_cb",NULL);

		if(port_str) {
			port = (short) atoi(port_str);
			ts->server_addr.sin_port = htons(port);
		}

		ts->server_addr.sin_family = AF_INET;
		if(destaddr_str) {
			rc = inet_pton(AF_INET,destaddr_str,&ts->server_addr.sin_addr);
			if(rc <= 0) {
				STK_LOG(STK_LOG_ERROR,"Could not convert destination address %s\n",destaddr_str);
				ts->server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
			}
		} else
			ts->server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

		ts->sock = socket(PF_INET, SOCK_DGRAM, 0);
		if(ts->sock == -1) {
			STK_LOG(STK_LOG_ERROR,"create rawudp client socket for data flow %p '%s'[%lu]",df,stk_data_flow_name(df),stk_get_data_flow_id(df));
			stk_free_data_flow(df);
			return NULL;
		}

		if(sndbuf_str)
			sndbuf = atoi(sndbuf_str);

		set_sock_buf_nice(ts->sock, SOL_SOCKET, SO_SNDBUF, sndbuf, port, env);

		if(mcastintf_str) {
			struct in_addr interface_addr;

			rc = inet_pton(AF_INET,mcastintf_str,&interface_addr);
			if(rc <= 0) {
				STK_LOG(STK_LOG_ERROR,"Could not convert interface address errno %d",errno);
				memset(&interface_addr,0,sizeof(interface_addr));
			}

			{
			unsigned char loop = 1;
			if(setsockopt(ts->sock, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop))) {
				STK_LOG(STK_LOG_ERROR,"Failed to set multicast loopback for data flow '%s'[%lu] to port %d, errno %d %s : env %p",name,id,port,errno,strerror(errno),env);
				close(ts->sock);
				stk_free_data_flow(df);
				return NULL;
			}
			}

			if(setsockopt (ts->sock, IPPROTO_IP, IP_MULTICAST_IF, &interface_addr, sizeof(interface_addr))) {
				STK_LOG(STK_LOG_ERROR,"Failed to set interface for data flow '%s'[%lu] to port %d, errno %d %s : env %p",name,id,port,errno,strerror(errno),env);
				close(ts->sock);
				stk_free_data_flow(df);
				return NULL;
			}
		}

		STK_LOG(STK_LOG_NORMAL,"data flow %p %s[%lu] to port %d created (fd %d)",df,stk_data_flow_name(df),stk_get_data_flow_id(df),ntohs(ts->server_addr.sin_port),ts->sock);

		if(ts->fd_created_cb)
			ts->fd_created_cb(ts->cb_df,stk_get_data_flow_id(ts->cb_df),ts->sock);
		return df;
	}

	return NULL;
}

stk_ret stk_rawudp_client_unhook_data_flow(stk_data_flow_t *df)
{
	stk_rawudp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */

	STK_API_DEBUG();

	if(ts->fd_destroyed_cb)
		ts->fd_destroyed_cb(ts->cb_df,stk_get_data_flow_id(ts->cb_df),ts->sock);

	close(ts->sock);
	ts->sock = -1;

	return STK_SUCCESS;
}

stk_ret stk_rawudp_client_destroy_data_flow(stk_data_flow_t *df)
{
	stk_ret ret;
	stk_rawudp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */

	STK_API_DEBUG();

	if(ts->sock != -1) {
		ret = stk_rawudp_client_unhook_data_flow(df);
		STK_ASSERT(STKA_NET,ret==STK_SUCCESS,"unhook fd %d for data flow %p",ts->sock,df);
	}

	ret = stk_free_data_flow(df);

	if(STK_ATOMIC_DECR(&timer_refcount) == 1) {
		stk_ret rc = stk_free_timer_set(stk_rawudp_client_timers,STK_TRUE);
		stk_rawudp_client_timers = NULL;
		STK_ASSERT(STKA_NET,rc == STK_SUCCESS,"free timer set for RAWUDP clients");
	}

	return ret;
}

int stk_rawudp_client_fd(stk_data_flow_t *df)
{
	stk_rawudp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	STK_API_DEBUG();
	return ts->sock;
}

stk_ret stk_rawudp_client_data_flow_sendbuf(stk_data_flow_t *df,char *buf,stk_uint64 buflen,stk_uint64 flags)
{
	return stk_rawudp_listener_data_flow_sendbuf(df,buf,buflen,flags);
}

stk_ret stk_rawudp_client_data_flow_send(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	return stk_rawudp_listener_data_flow_send(df,data_sequence,flags);
}

stk_ret stk_rawudp_client_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	stk_rawudp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	STK_API_DEBUG();
	memcpy(data_flow_id,&ts->client_addr,addrlen);
	return STK_SUCCESS;
}

stk_ret stk_rawudp_client_data_flow_serverip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	stk_rawudp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	STK_API_DEBUG();
	memcpy(data_flow_id,&ts->server_addr,addrlen);
	return STK_SUCCESS;
}

stk_ret stk_rawudp_client_data_flow_buffered(stk_data_flow_t *df)
{
	stk_rawudp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	STK_API_DEBUG();
	return !STK_SUCCESS;
}

