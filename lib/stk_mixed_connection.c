
/* This was abandoned.
 * Originally implemented to provide a send and receive UDP data flow until I realized it could be done on the same socket.
 * However, its also designed to handle tcp udp combined data flows too - this has yet to be developed.
 */
#include "stk_data_flow_api.h"
#include "stk_data_flow.h"
#include "stk_common.h"
#include "stk_internal.h"
#include "stk_env_api.h"
#include "stk_sequence.h"
#include "stk_mixed_connection_api.h"
#include "stk_udp_client_api.h"
#include "stk_udp_listener_api.h"
#include "stk_udp_internal.h"
#include "stk_options_api.h"
#include "stk_timer_api.h"
#include "stk_sequence_api.h"
#include "stk_sync.h"
#include "stk_udp.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

/* copied from stk_tcp_server.c */
#ifdef __APPLE__
#define STK_NB_SEND_FLAGS 0
#else
#define STK_NB_SEND_FLAGS MSG_NOSIGNAL
#endif

stk_timer_set_t *stk_mixed_connection_timers;
static int timer_refcount;

stk_ret stk_mixed_connection_destroy_data_flow(stk_data_flow_t *flow);
stk_ret stk_mixed_connection_data_flow_send(stk_data_flow_t *flow,stk_sequence_t *data_sequence,stk_uint64 flags);
stk_ret stk_mixed_connection_data_flow_buffered(stk_data_flow_t *df);

static stk_data_flow_module_t mixed_connection_fptrs = {
	stk_mixed_connection_create_data_flow, stk_mixed_connection_destroy_data_flow,
	stk_mixed_connection_data_flow_send, stk_mixed_connection_data_flow_rcv,
	stk_mixed_connection_data_flow_id_ip, stk_mixed_connection_data_flow_buffered
};

typedef struct stk_mixed_connection_stct {
	stk_data_flow_t *client_df;
	stk_data_flow_t *listener_df;
} stk_mixed_connection_t;

typedef struct stk_udp_wire_send_buf_stct {
	int dummy;
} stk_udp_wire_send_buf_t; /* Better to have separate send/recv control structs? */

//#define STK_UDP_DBG printf
#ifndef STK_UDP_DBG
#define STK_UDP_DBG(...) 
#endif

stk_data_flow_t *stk_mixed_connection_create_data_flow(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options)
{
	stk_data_flow_t *df = stk_alloc_data_flow(env,STK_UDP_CLIENT_FLOW,name,id,sizeof(stk_mixed_connection_t),&mixed_connection_fptrs,options);
	stk_mixed_connection_t *ts = df ? stk_data_flow_module_data(df) : NULL;
	void *udp_listener = stk_find_option(options,"udp_listener",NULL);
	void *udp_client = stk_find_option(options,"udp_client",NULL);

	if(udp_listener && udp_client) {
		ts->client_df = stk_udp_client_create_data_flow(env,name,id,options);
		if(!ts->client_df) {
			STK_LOG(STK_LOG_ERROR,"create mixed connection client for data flow '%s'[%lu], env %p",name,id,env);
			stk_free_data_flow(df);
			return NULL;
		}
		ts->listener_df = stk_udp_listener_create_data_flow(env,name,id,options);
		if(!ts->listener_df) {
			STK_LOG(STK_LOG_ERROR,"create mixed connection listener for data flow '%s'[%lu], env %p",name,id,env);
			stk_free_data_flow(df);
			return NULL;
		}

		{
		struct sockaddr_in server_addr;
		stk_udp_client_data_flow_serverip(ts->client_df,(struct sockaddr *) &server_addr,sizeof(server_addr));
		STK_LOG(STK_LOG_NORMAL,"data flow %p %s[%lu] to port %d created (fds client %d listener %d)",
			df,stk_data_flow_name(df),stk_get_data_flow_id(df),ntohs(server_addr.sin_port),
			stk_udp_client_fd(ts->client_df),stk_udp_listener_fd(ts->listener_df));
		}

		return df;
	} else {
		STK_LOG(STK_LOG_ERROR,"Must specify udp_listener and udp_client in stk_mixed_connection_create_data_flow");
		return NULL;
	}
}

stk_ret stk_mixed_connection_destroy_data_flow(stk_data_flow_t *df)
{
	stk_ret ret;
	stk_mixed_connection_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */

	STK_API_DEBUG();

	if(ts->listener_df) {
		ret = stk_destroy_data_flow(ts->listener_df);
		STK_ASSERT(STKA_NET,ret==STK_SUCCESS,"unhook df %p for data flow %p",ts->listener_df,df);
	}

	if(ts->client_df) {
		ret = stk_destroy_data_flow(ts->client_df);
		STK_ASSERT(STKA_NET,ret==STK_SUCCESS,"unhook df %p for data flow %p",ts->client_df,df);
	}
	ret = stk_free_data_flow(df);

	return ret;
}

stk_ret stk_mixed_connection_data_flow_send(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	stk_mixed_connection_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	return stk_data_flow_send(ts->listener_df,data_sequence,flags);
}

stk_sequence_t *stk_mixed_connection_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	stk_mixed_connection_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	return stk_data_flow_rcv(ts->listener_df,data_sequence,flags);
}

int stk_mixed_connection_listener_fd(stk_data_flow_t *svr_df)
{
	stk_mixed_connection_t *ts = stk_data_flow_module_data(svr_df); /* Asserts on structure type */
	return stk_udp_listener_fd(ts->listener_df);
}

int stk_mixed_connection_client_fd(stk_data_flow_t *client_df)
{
	stk_mixed_connection_t *ts = stk_data_flow_module_data(client_df); /* Asserts on structure type */
	return stk_udp_client_fd(ts->client_df);
}

stk_ret stk_mixed_connection_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	stk_mixed_connection_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	return stk_udp_listener_data_flow_id_ip(ts->listener_df,data_flow_id,addrlen);
}

stk_ret stk_mixed_connection_data_flow_buffered(stk_data_flow_t *df)
{
	stk_mixed_connection_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	return stk_data_flow_buffered(ts->listener_df);
}

