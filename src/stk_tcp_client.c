#include "stk_data_flow_api.h"
#include "stk_data_flow.h"
#include "stk_common.h"
#include "stk_internal.h"
#include "stk_env_api.h"
#include "stk_sequence.h"
#include "stk_tcp_client_api.h"
#include "stk_tcp_server_api.h"
#include "stk_tcp_internal.h"
#include "stk_options_api.h"
#include "stk_timer_api.h"
#include "stk_sync_api.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#define STK_TCP_BACKLOG 1024
#define STK_CACHED_READBUF_SZ 64*1024

stk_timer_set_t *stk_tcp_client_timers;
static int timer_refcount;

stk_ret stk_tcp_client_destroy_data_flow(stk_data_flow_t *flow);
stk_ret stk_tcp_client_data_flow_send(stk_data_flow_t *flow,stk_sequence_t *data_sequence,stk_uint64 flags);
stk_sequence_t *stk_tcp_client_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);
void stk_tcp_client_reconnect_cb(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type);
stk_ret stk_tcp_client_data_flow_buffered(stk_data_flow_t *df);
char *stk_tcp_client_data_flow_protocol(stk_data_flow_t *df);

static stk_data_flow_module_t tcp_client_fptrs = {
	stk_tcp_client_create_data_flow, stk_tcp_client_destroy_data_flow,
	stk_tcp_client_data_flow_send, stk_tcp_client_data_flow_rcv,
	stk_tcp_client_data_flow_id_ip, stk_tcp_client_data_flow_buffered,
	stk_tcp_client_data_flow_protocol
};

typedef struct stk_tcp_client_stct {
	int sock;
	short port;
	struct sockaddr_in server_addr;
	struct sockaddr_in client_addr;
	stk_tcp_wire_read_buf_t readbuf;
	stk_timer_t *reconnect_timer;
	stk_data_flow_fd_created_cb fd_created_cb;
	stk_data_flow_fd_destroyed_cb fd_destroyed_cb;
	int nodelay;
	int sndbuf;
	int rcvbuf;
	int reconnect_ivl;
	short seq_connect_failures;
} stk_tcp_client_t;

stk_ret stk_tcp_client_connect(stk_data_flow_t *df);

stk_data_flow_t *stk_tcp_client_create_data_flow(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options)
{
	stk_data_flow_t *df = stk_alloc_data_flow(env,STK_TCP_CLIENT_FLOW,name,id,sizeof(stk_tcp_client_t),&tcp_client_fptrs,options);
	stk_tcp_client_t *ts = df ? stk_data_flow_module_data(df) : NULL;
	int rc;

	STK_API_DEBUG();

	if(stk_tcp_client_timers == NULL) {
		stk_tcp_client_timers = stk_new_timer_set(env,NULL,0,STK_TRUE);
		STK_ASSERT(STKA_NET,stk_tcp_client_timers!=NULL,"allocate a timer set for TCP clients");
	}

	if(df) {
		short port = 29090;

		STK_ATOMIC_INCR(&timer_refcount);

		{
		void *destaddr_str = stk_find_option(options,"destination_address",NULL);
		void *destport_str = stk_find_option(options,"destination_port",NULL);
		void *connectaddr_str = destaddr_str ? destaddr_str : stk_find_option(options,"connect_address",NULL);
		void *port_str = destport_str ? destport_str : stk_find_option(options,"connect_port",NULL);
		void *sndbuf_str = stk_find_option(options,"send_buffer_size",NULL);
		void *rcvbuf_str = stk_find_option(options,"receive_buffer_size",NULL);
		void *nodelay_str = stk_find_option(options,"nodelay",NULL);
		void *reconnect_str = stk_find_option(options,"reconnect_interval",NULL);

		ts->sock = -1;

		ts->fd_created_cb = (stk_data_flow_fd_created_cb) stk_find_option(options,"fd_created_cb",NULL);
		ts->fd_destroyed_cb = (stk_data_flow_fd_destroyed_cb) stk_find_option(options,"fd_destroyed_cb",NULL);

		if(nodelay_str) {
			ts->nodelay = 1;
		}

		if(sndbuf_str)
			ts->sndbuf = atoi(sndbuf_str);

		if(rcvbuf_str)
			ts->rcvbuf = atoi(rcvbuf_str);

		if(reconnect_str)
			ts->reconnect_ivl = atoi(reconnect_str);
		else
			ts->reconnect_ivl = 5;

		ts->server_addr.sin_family = AF_INET;
		if(connectaddr_str) {
			rc = inet_pton(AF_INET,connectaddr_str,&ts->server_addr.sin_addr);
			if(rc <= 0) {
				STK_LOG(STK_LOG_ERROR,"Could not convert connect address %s\n",connectaddr_str);
				ts->server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
			}
		} else
			ts->server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

		if(port_str)
			port = (short) atoi(port_str);
			ts->server_addr.sin_port = htons(port);
		}

		/* Create a caching read buffer for this data flow */
		ts->readbuf.buf = STK_ALLOC_BUF(STK_CACHED_READBUF_SZ);
		ts->readbuf.sz = STK_CACHED_READBUF_SZ;
		ts->readbuf.df = df;

		rc = stk_tcp_client_connect(df);
		if(rc == STK_SUCCESS) {
			if(ts->fd_created_cb)
				ts->fd_created_cb(df,stk_get_data_flow_id(df),ts->sock);
		} else {
			/* stk_tcp_client_connect already logged */
			close(ts->sock);
			ts->sock = -1;

			/* start reconnect timer */
			ts->reconnect_timer = stk_schedule_timer(stk_tcp_client_timers,stk_tcp_client_reconnect_cb,0,df,ts->reconnect_ivl);
			STK_ASSERT(STKA_NET,ts->reconnect_timer!=NULL,"start reconnect timer for data flow %p",df);
		}
		return df;
	}

	return NULL;
}

stk_ret stk_tcp_client_unhook_data_flow(stk_data_flow_t *df)
{
	stk_tcp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */

	STK_API_DEBUG();

	if(ts->fd_destroyed_cb)
		ts->fd_destroyed_cb(df,stk_get_data_flow_id(df),ts->sock);

	close(ts->sock);
	ts->sock = -1;

	return STK_SUCCESS;
}

stk_ret stk_tcp_client_destroy_data_flow(stk_data_flow_t *df)
{
	stk_ret ret;
	stk_tcp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */

	STK_API_DEBUG();

	if(ts->reconnect_timer) {
		stk_ret rc = stk_cancel_timer(stk_tcp_client_timers,ts->reconnect_timer);
		STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"cancel reconnect timer for data flow %p",df);
	}

	if(ts->sock != -1) {
		ret = stk_tcp_client_unhook_data_flow(df);
		STK_ASSERT(STKA_NET,ret==STK_SUCCESS,"unhook fd %d for data flow %p",ts->sock,df);
	}

	if(ts->readbuf.buf) STK_FREE(ts->readbuf.buf);
	ret = stk_free_data_flow(df);

	if(STK_ATOMIC_DECR(&timer_refcount) == 1) {
		stk_ret rc = stk_free_timer_set(stk_tcp_client_timers,STK_TRUE);
		stk_tcp_client_timers = NULL;
		STK_ASSERT(STKA_NET,rc == STK_SUCCESS,"free timer set for TCP clients");
	}

	return ret;
}

/* Internal function */
stk_ret stk_tcp_client_connect(stk_data_flow_t *df)
{
	stk_tcp_client_t *ts = stk_data_flow_module_data(df);

	STK_ASSERT(STKA_NET,ts->sock==-1,"connecting on an unclosed socket %d",ts->sock);
	ts->sock = socket(PF_INET, SOCK_STREAM, 0);
	if(ts->sock == -1) {
		STK_LOG(STK_LOG_ERROR,"create server socket for data flow %p '%s'[%lu]",df,stk_data_flow_name(df),stk_get_data_flow_id(df));
		return !STK_SUCCESS;
	}

	if(ts->nodelay) {
		int rc = setsockopt(ts->sock, IPPROTO_TCP, TCP_NODELAY, &ts->nodelay, sizeof(ts->nodelay));
		if(rc < 0)
			STK_LOG(STK_LOG_ERROR,"set client socket no delay on port %d, env %p",ntohs(ts->server_addr.sin_port),stk_env_from_data_flow(df));
	}
	if(ts->sndbuf) {
		int rc = setsockopt(ts->sock, SOL_SOCKET, SO_SNDBUF, &ts->sndbuf, sizeof(ts->sndbuf));
		if(rc < 0)
			STK_LOG(STK_LOG_ERROR,"set client socket send buffer size on port %d, env %p",ntohs(ts->server_addr.sin_port),stk_env_from_data_flow(df));
	}
	if(ts->rcvbuf) {
		int rc = setsockopt(ts->sock, SOL_SOCKET, SO_RCVBUF, &ts->rcvbuf, sizeof(ts->rcvbuf));
		if(rc < 0)
			STK_LOG(STK_LOG_ERROR,"set client socket receive buffer size on port %d, env %p",ntohs(ts->server_addr.sin_port),stk_env_from_data_flow(df));
	}

	if(connect(ts->sock,(struct sockaddr *) &ts->server_addr,sizeof(ts->server_addr)) == -1) {
		int err = errno;
		/* Control logging of connect failures and indicate THROTTLED if more than once sequentially */
		if(ts->seq_connect_failures == 0 || ts->seq_connect_failures == 1) {
			STK_LOG(STK_LOG_NET_ERROR,"%sFailed to connect to server data flow %p '%s'[%lu] to port %d, errno %d %s : env %p",
				ts->seq_connect_failures == 1 ? "*THROTTLED* " : "",
				df,stk_data_flow_name(df),stk_get_data_flow_id(df),
				ntohs(ts->server_addr.sin_port),err,strerror(err),stk_env_from_data_flow(df));
			if(ts->seq_connect_failures < 2)
				ts->seq_connect_failures++;
		}
		return !STK_SUCCESS;
	}
	ts->seq_connect_failures = 0;

	STK_LOG(STK_LOG_NORMAL,"data flow %p %s[%lu] to port %d connected (fd %d)",df,stk_data_flow_name(df),stk_get_data_flow_id(df),ntohs(ts->server_addr.sin_port),ts->sock);

	{
	socklen_t len = sizeof(ts->client_addr);
	if (getsockname(ts->sock, (struct sockaddr *)&ts->client_addr, &len) == -1) {
		int err = errno;
		STK_LOG(STK_LOG_ERROR,"Failed to get peer address server socket for data flow %p '%s'[%lu] to port %d, errno %d %s : env %p",df,stk_data_flow_name(df),stk_get_data_flow_id(df),ntohs(ts->server_addr.sin_port),err,strerror(err),stk_env_from_data_flow(df));
		return !STK_SUCCESS;
	}
	/* Convert to host order */
	ts->client_addr.sin_port = ntohs(ts->client_addr.sin_port);
	ts->client_addr.sin_addr.s_addr = ntohl(ts->client_addr.sin_addr.s_addr);
	STK_CHECK(STKA_NET,ts!=NULL,"Client IP %x:%d data flow %p",ts->client_addr.sin_addr.s_addr,ts->client_addr.sin_port,df);
	}

	/* Use non blocking mode now we've connected */
	{
	int rc = fcntl(ts->sock, F_SETFL, O_NONBLOCK);
	if(rc == -1) {
		STK_LOG(STK_LOG_ERROR,"Failed to set non blocking mode on socket for data flow %p %s[%lu]",df,stk_data_flow_name(df),stk_get_data_flow_id(df));
		return !STK_SUCCESS;
	}
	}
	return STK_SUCCESS;
}

void stk_tcp_client_reconnect_cb(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type)
{
	if(cb_type == STK_TIMER_EXPIRED) {
		stk_data_flow_t *df = (stk_data_flow_t *) userdata;
		stk_tcp_client_t *ts = stk_data_flow_module_data(df);

		stk_ret rc = stk_tcp_client_connect((stk_data_flow_t *) userdata);
		if(rc != STK_SUCCESS) {
			close(ts->sock);
			ts->sock = -1;

			rc = stk_reschedule_timer(timer_set,timer);
			STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"reschedule reconnect timer for tcp client %p",userdata);
		}
		else
		if(ts->fd_created_cb)
			ts->fd_created_cb(df,stk_get_data_flow_id(df),ts->sock);
	}
}

int stk_tcp_client_fd(stk_data_flow_t *df)
{
	stk_tcp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	STK_API_DEBUG();
	return ts->sock;
}

/* These are fakes right now and just reuse the server APIs */
stk_ret stk_tcp_client_data_flow_send(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	stk_tcp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	STK_API_DEBUG();
	if(ts->sock == -1) return !STK_SUCCESS;

	stk_ret rc = stk_tcp_server_data_flow_send(df,data_sequence,flags);
	if(rc == STK_RESET) {
		stk_ret ret = stk_tcp_client_unhook_data_flow(df);
		STK_ASSERT(STKA_NET,ret==STK_SUCCESS,"unhook fd %d for data flow %p",ts->sock,df);

		/* connection dropped, start reconnect timer */
		ts->reconnect_timer = stk_schedule_timer(stk_tcp_client_timers,stk_tcp_client_reconnect_cb,0,df,5);
		STK_ASSERT(STKA_NET,ts->reconnect_timer!=NULL,"start reconnect timer for data flow %p",df);
	}

	return rc;
}

stk_sequence_t *stk_tcp_client_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	stk_tcp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	STK_API_DEBUG();
	if(ts->sock == -1) return NULL; /* Unhooked */

	{
	stk_sequence_t *ret_seq = stk_tcp_server_data_flow_rcv(df,data_sequence,flags);

	if(ret_seq == NULL && stk_data_flow_errno(df) != 0) {

		stk_ret ret = stk_tcp_client_unhook_data_flow(df);
		STK_ASSERT(STKA_NET,ret==STK_SUCCESS,"unhook fd %d for data flow %p",ts->sock,df);

		/* connection dropped, start reconnect timer */
		ts->reconnect_timer = stk_schedule_timer(stk_tcp_client_timers,stk_tcp_client_reconnect_cb,0,df,5);
		STK_ASSERT(STKA_NET,ts->reconnect_timer!=NULL,"start reconnect timer for data flow %p",df);
	}
	return ret_seq;
	}
}

stk_ret stk_tcp_client_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	stk_tcp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	STK_API_DEBUG();
	memcpy(data_flow_id,&ts->client_addr,addrlen);
	return STK_SUCCESS;
}

stk_ret stk_tcp_client_data_flow_serverip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	stk_tcp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	STK_API_DEBUG();
	memcpy(data_flow_id,&ts->server_addr,addrlen);
	return STK_SUCCESS;
}

stk_ret stk_tcp_client_data_flow_buffered(stk_data_flow_t *df)
{
	stk_tcp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	STK_API_DEBUG();
	return stk_tcp_data_buffered(&ts->readbuf) > 0 ? STK_SUCCESS : !STK_SUCCESS;
}

char *stk_tcp_client_data_flow_protocol(stk_data_flow_t *df) { return "tcp"; }

