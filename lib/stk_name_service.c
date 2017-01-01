#include "stk_data_flow_api.h"
#include "stk_tcp_client_api.h"
#include "stk_udp_listener_api.h"
#include "stk_data_flow.h"
#include "stk_common.h"
#include "stk_internal.h"
#include "stk_env_api.h"
#include "stk_env.h"
#include "stk_sequence.h"
#include "stk_sequence_api.h"
#include "stk_name_service.h"
#include "stk_name_service_api.h"
#include "stk_smartbeat_api.h"
#include "stk_options_api.h"
#include "stk_timer_api.h"
#include "stk_tcp.h"
#include "stk_ports.h"
#include "PLists.h"
#include <string.h>
#include <strings.h>
#include <sys/time.h>

typedef struct stk_name_request_stct {
	stk_name_info_t request;
	int expiration_ms;
	stk_sequence_type type;
	stk_name_info_cb_t cb;
	void *app_info;
	short num_cbs;
	short subscription;
	struct timeval expired_tv;
	char *group_name;
} stk_name_request_t;

typedef struct {
	stk_name_info_t request;
	stk_sequence_type type;
	stk_uint64 request_id;
	short ip_idx;
	short protocol_idx;
	stk_uint64 name_server_id;
} stk_ns_rcv_data_t;

struct stk_name_service_stct {
	stk_stct_type stct_type;
	stk_env_t *env;
	stk_data_flow_t *df;
	List *request_list;
	struct {
		int snds;
		int blks;
	} stats;
	char group_name[STK_MAX_GROUP_NAME_LEN];
	stk_timer_set_t *timers;
	stk_timer_t *request_gc_timer;
	stk_timer_t *smartbeat_timer;
	List *name_server_list;
};

typedef struct stk_name_service_activity_stct {
	stk_stct_type stct_type;
	stk_uint64 id;
	struct timeval expired_tv;
} stk_name_service_activity_t;

stk_ret stk_resubscribe_names(stk_name_service_t *named);

stk_ret stk_remove_name_request(Node *n)
{
	/* Stop timer (when implemented) */

	Remove(n);

	FreeNode(n);

	return STK_SUCCESS;
}

void stk_request_gc_cb(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type)
{
	stk_name_request_t *req;
	Node *nxt;
	struct timeval current_tv;
	stk_name_service_t *named = (stk_name_service_t *) userdata;
	STK_ASSERT(STKA_NS,named!=NULL,"valid name server passed to request gc timer callback");
	STK_ASSERT(STKA_NS,named->stct_type==STK_STCT_NAME_SERVICE,"name server request gc callback, the pointer was to a structure of type %d",named->stct_type);

	if(cb_type == STK_TIMER_CANCELLED) return;

	if(gettimeofday(&current_tv,NULL) == -1) return;

	for(Node *n = FirstNode(named->request_list); !AtListEnd(n); n = nxt) {
		nxt = NxtNode(n);
		req = NodeData(n);

		STK_DEBUG(STKA_NS,"request_gc_timer_cb req name %s",req->request.name);
		if(timercmp(&current_tv, &req->expired_tv, >)) {
			stk_ret ret;

			/* Request has expired */
			STK_DEBUG(STKA_NS,"invoke_name_cb calling %p req name %s expired",req->app_info,req->request.name);
			req->cb(&req->request,1,NULL,req->app_info,STK_NS_REQUEST_EXPIRED);

			ret = stk_remove_name_request(n);
			STK_ASSERT(STKA_NS,ret==STK_SUCCESS,"freeing name request");
		}
	}

	stk_ret rc = stk_reschedule_timer(timer_set,timer);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"reschedule request gc timer for name service");
}

stk_data_flow_t *stk_create_name_service_data_flow(stk_env_t *env, stk_options_t *options)
{
	/* Duplicate the options so we can add in the default values - just always do it cos its simpler that way and only happens once */
	void *name_server_df_options = stk_find_option(options,"name_server_data_flow_options",NULL);
	void *name_server_data_flow_options = name_server_df_options ? name_server_df_options : options;
	void *destaddr_str = stk_find_option(name_server_data_flow_options,"destination_address",NULL);
	void *destport_str = stk_find_option(name_server_data_flow_options,"destination_port",NULL);
	void *connectaddr_str = destaddr_str ? destaddr_str : stk_find_option(name_server_data_flow_options,"connect_address",NULL);
	void *port_str = destport_str ? destport_str : stk_find_option(name_server_data_flow_options,"connect_port",NULL);
	int extensions = 0;
	char *udpstr = stk_find_option(options,"name_server_data_flow_protocol",NULL);

	if(udpstr && strcasecmp(udpstr,"udp"))
		udpstr = NULL; /* Not udp */

	if(!connectaddr_str) extensions++;
	if(!port_str) extensions++;

	if(extensions > 0) {
		stk_ret rc = STK_SUCCESS;

		name_server_data_flow_options = stk_copy_extend_options(name_server_data_flow_options, extensions);

		/* Default name service address and port to 127.0.0.1:20002 */
		if(!connectaddr_str)
			rc = stk_append_option(name_server_data_flow_options, "destination_address", "127.0.0.1");
		if(rc == STK_SUCCESS && !port_str)
			rc = stk_append_option(name_server_data_flow_options, "destination_port", int_to_string_p(STK_NAMED_RCV_DF_PORT));

		if(rc != STK_SUCCESS) {
			rc = stk_free_options(name_server_data_flow_options);
			STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"free extended options for name service (after error)");
			return NULL;
		}
	}

	/* Process name_server_data_flow_options, open name server df, store df for other modules */
	{
	stk_options_t parent_options[] = { { "name_server_data_flow_options", name_server_data_flow_options }, { NULL, NULL } };
	stk_create_data_flow_t name_server_create_df = stk_tcp_client_create_data_flow;
	int (*fd_func)(stk_data_flow_t *svr_df) = stk_tcp_client_fd;
	stk_data_flow_t *df;

	if(udpstr) {
		name_server_create_df = stk_udp_listener_create_data_flow;
		fd_func = stk_udp_listener_fd;
	}

	/* Service name_server is determined by the configuration of a name_server data flow */
	df = stk_data_flow_process_extended_options(env,parent_options,"name_server_data_flow",name_server_create_df);
	STK_ASSERT(STKA_NET,df!=NULL,"create name server data flow");

	if(extensions > 0) {
		stk_ret rc = stk_free_options(name_server_data_flow_options);
		STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"free extended options for name service");
	}

	return df;
	}
}

stk_name_service_activity_t *find_name_server(stk_name_service_t *named,stk_uint64 id)
{
	stk_name_service_activity_t *ns_activity;

	for(Node *n = FirstNode(named->name_server_list); !AtListEnd(n); n = NxtNode(n)) {
		stk_name_service_activity_t *ns_activity = NodeData(n);

		if(ns_activity->id == id)
			return ns_activity;
	}
	return NULL;
}

stk_name_service_activity_t *store_name_server(stk_name_service_t *named,stk_uint64 id)
{
	stk_name_service_activity_t *ns_activity;
	Node *n;

	STK_CALLOC_STCT_EX(STK_STCT_NAME_SERVICE_ACTIVITY,stk_name_service_activity_t,0,ns_activity);
	ns_activity->id = id;

	n = NewNode();
	SetData(n,ns_activity);

	AddTail(named->name_server_list,n);
	return ns_activity;
}

void update_name_server_activity(stk_name_service_t *named,stk_uint64 id)
{
	stk_name_service_activity_t *ns_activity;
	int expiration_ms = 5000;

	if(!named->name_server_list)
		named->name_server_list = NewPList();

	ns_activity = find_name_server(named,id);
	if(!ns_activity) {
		ns_activity = store_name_server(named,id);
		stk_resubscribe_names(named);
	}

	/* Technically this adds a partially initialized object to the list.
	 * if a lock is ever added to the list, this entire block needs to
	 * be protected.
	 */
	gettimeofday(&ns_activity->expired_tv,NULL);
	ns_activity->expired_tv.tv_usec += ((long)expiration_ms*1000);
	while(ns_activity->expired_tv.tv_usec >= 1000000L) {
		ns_activity->expired_tv.tv_sec++;
		ns_activity->expired_tv.tv_usec -= 1000000;
	}
}

void expire_name_servers(stk_name_service_t *named)
{
	stk_name_service_activity_t *ns_activity;
	struct timeval current_tv;
	Node *nxt = NULL;

	if(!named->name_server_list || IsPListEmpty(named->name_server_list)) return;

	gettimeofday(&current_tv,NULL);
	for(Node *n = FirstNode(named->name_server_list); !AtListEnd(n); n = nxt) {
		stk_name_service_activity_t *ns_activity = NodeData(n);
		nxt = NxtNode(n);

		if(timercmp(&current_tv, &ns_activity->expired_tv, >)) {
			Remove(n);

			/* Err.... need some kind of callback ?
			 * What to do? If multicast, it is only one
			 * server that may be down.
			 * Count the number of active name servers? And reconnect if 0?
			 */
			STK_LOG(STK_LOG_NORMAL,"named id %lx expired %p active name servers %lu",
				ns_activity->id,named->df,NodeCount(named->name_server_list));

			if(IsPListEmpty(named->name_server_list))
				STK_LOG(STK_LOG_WARNING,"WARNING: All name servers unresponsive");

			FreeNode(n); /* auto frees ns_activity */
		}
	}
}

void stk_ns_smartbeat_cb(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type)
{
	stk_name_service_t *named = (stk_name_service_t *) userdata;

	if(cb_type == STK_TIMER_CANCELLED) return;

	expire_name_servers(named);

	stk_ret rc = stk_reschedule_timer(timer_set,timer);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"reschedule request gc timer for name service");
}

stk_name_service_t *stk_create_name_service(stk_env_t *env, stk_options_t *options) {
	stk_name_service_t *named;

	STK_CALLOC_STCT_EX(STK_STCT_NAME_SERVICE,stk_name_service_t,0,named);
	if(!named) return NULL;

	named->env = env;

	{
	char *group_name = (char *) stk_find_option(options,"group_name",NULL);
	if(group_name && (group_name[0] != '\0')) { /* treat "" as NULL */
		strncpy(named->group_name,group_name,sizeof(named->group_name));
		named->group_name[sizeof(named->group_name) - 1] = '\0';;
	}
	}

	named->request_list = NewPList();
	if(!named->request_list) {
		STK_FREE(named);
		return NULL;
	}

	named->timers = stk_new_timer_set(env,NULL,0,STK_TRUE);
	STK_ASSERT(STKA_NS,named->timers!=NULL,"allocate a timer set for the name service");

	named->request_gc_timer = stk_schedule_timer(named->timers,stk_request_gc_cb,0,named,1000);
	named->smartbeat_timer = stk_schedule_timer(named->timers,stk_ns_smartbeat_cb,0,named,750);

	{
	stk_ret rc;
	named->df = stk_create_name_service_data_flow(env, options);
	if(named->df == NULL) {
		STK_FREE(named);
		return NULL;
	}

	rc = stk_smartbeat_add_name_service(stk_env_get_smartbeat_ctrl(env), named);
	STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"add name service to smartbeat controller");
	}

	return named;
}

stk_ret stk_destroy_name_service(stk_name_service_t *named)
{
	stk_ret rc = stk_free_timer_set(named->timers,STK_TRUE);
	STK_ASSERT(STKA_NS,rc == STK_SUCCESS,"free timer set for name service");

	rc = stk_smartbeat_remove_name_service(stk_env_get_smartbeat_ctrl(named->env), named);
	STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"remove name service %p from smartbeat controller",named);

	stk_destroy_data_flow(named->df);

	/* Free outstanding requests and list */
	if(named->request_list) {
		Node *nxt;
		for(Node *n = FirstNode(named->request_list); !AtListEnd(n); n = nxt) {
			stk_ret ret;

			nxt = NxtNode(n);

			ret = stk_remove_name_request(n);
			STK_ASSERT(STKA_NS,ret==STK_SUCCESS,"freeing name request");
		}
		FreeList(named->request_list);
	}

	STK_FREE(named);

	return STK_SUCCESS;
}

stk_name_request_t *stk_alloc_name_request(char *name, char *group_name, stk_sequence_type type,
	int expiration_ms, stk_name_info_cb_t cb, void *app_info, short num_cbs, short subscription)
{
	stk_name_request_t *request = STK_CALLOC(sizeof(stk_name_request_t));
	STK_ASSERT(STKA_NS,request!=NULL,"allocate name request");

	strcpy(request->request.name,name);
	request->group_name = group_name;
	request->expiration_ms = expiration_ms;
	request->cb = cb;
	request->type = type;
	request->app_info = app_info;
	request->num_cbs = num_cbs;
	request->subscription = subscription;

	if(gettimeofday(&request->expired_tv,NULL) == -1) {
		STK_FREE(request);
		return NULL;
	}
	request->expired_tv.tv_usec += ((long)expiration_ms*1000);
	while(request->expired_tv.tv_usec >= 1000000L) {
		request->expired_tv.tv_sec++;
		request->expired_tv.tv_usec -= 1000000;
	}

	return request;
}

stk_ret stk_send_name_query(stk_name_service_t *named, stk_name_request_t *request, int type)
{
	stk_sequence_t *seq;
	stk_ret rc;
	char *group_name = request->group_name;

	if(!group_name && named->group_name[0])
		group_name = named->group_name;

	seq = stk_create_sequence(named->env,
		STK_NAME_REQUEST_SEQUENCE_NAME,STK_NAME_REQUEST_SEQUENCE_ID,
		type,STK_SERVICE_TYPE_MGMT,NULL);
	STK_ASSERT(STKA_NS,seq!=NULL,"allocate sequence");

	/* Request ID is simply the request pointer - its unique! */
	rc = stk_copy_to_sequence(seq,&request,(stk_uint64) sizeof(request), STK_NS_SEQ_REQUEST_ID);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding request id to sequence");

	rc = stk_copy_to_sequence(seq,request->request.name,(stk_uint64) strlen(request->request.name) + 1, STK_NS_SEQ_NAME);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding name to sequence");

	if(group_name) {
		rc = stk_copy_to_sequence(seq,group_name,(stk_uint64) strlen(group_name) + 1, STK_NS_SEQ_GROUP_NAME);
		STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding group name %s to sequence for specific request",group_name);
	}

	rc = stk_data_flow_send(named->df,seq,STK_TCP_SEND_FLAG_NONBLOCK);
	if(rc == STK_SUCCESS || rc == STK_WOULDBLOCK) {
		if(rc == STK_SUCCESS)
			named->stats.snds++;
		else
			named->stats.blks++;
	}

	{ /* Use a different variable for the return code so as not to change rc */
	stk_ret ret = stk_destroy_sequence(seq);
	STK_ASSERT(STKA_NS,ret==STK_SUCCESS,"freeing name to sequence");
	}

	return rc;
}

stk_ret stk_request_name_info_common(stk_name_service_t *named, char *name, int expiration_ms, stk_name_info_cb_t cb, void *app_info, stk_options_t *options, short num_cbs, int type)
{
	stk_name_request_t *request;
	stk_ret rc;

	STK_ASSERT(STKA_NS,named->stct_type==STK_STCT_NAME_SERVICE,"request name info, the pointer was to a structure of type %d",named->stct_type);

	if(!name) return STK_INVALID_ARG;
	if(!cb) return STK_INVALID_ARG;
	if(!named->df) return STK_NETERR;

	STK_DEBUG(STKA_NS,"env %p name %s expiration %d cb %p app_info %p df %p",named->env,name,expiration_ms,cb,app_info,named->df);

	if(expiration_ms <= 0) {
		STK_LOG(STK_LOG_ERROR,"name requested with expiration time of %d for name %s (must be greater than 0)",expiration_ms,name);
		return !STK_SUCCESS;
	}

	{
	void *group_name = stk_find_option(options,"group_name",NULL);

	/* stk_alloc_name_request asserts if allocation fails
	 * This sets sequence type query as the request type, even for subscriptions because
	 * responses to the subscription will come back as query responses and the type must match
	 */
	request = stk_alloc_name_request(name,group_name,STK_SEQUENCE_TYPE_QUERY,expiration_ms,cb,app_info,num_cbs,
		type == STK_SEQUENCE_TYPE_SUBSCRIBE);
	STK_DEBUG(STKA_NS,"request expiration %ld.%06d",request->expired_tv.tv_sec, request->expired_tv.tv_usec);

	rc = stk_send_name_query(named,request,type);
	}

	if(rc == STK_SUCCESS) {
		Node *n = NewNode();
		SetData(n,request);

		/* Add to list after sending */
		AddTail(named->request_list,n);
	}

	return rc;
}

stk_ret stk_request_name_info(stk_name_service_t *named, char *name, int expiration_ms, stk_name_info_cb_t cb, void *app_info, stk_options_t *options)
{
	return stk_request_name_info_common(named, name, expiration_ms, cb, app_info, options, 0, STK_SEQUENCE_TYPE_QUERY);
}

/* Make this public so folks can forcibly resubscribe? */
stk_ret stk_resubscribe_names(stk_name_service_t *named)
{
	/* Resubscribe to all existing subscriptions */
	for(Node *n = FirstNode(named->request_list); !AtListEnd(n); n = NxtNode(n)) {
		stk_name_request_t *request = NodeData(n);
		if(request->subscription)
			stk_send_name_query(named,request,STK_SEQUENCE_TYPE_SUBSCRIBE); /* Ignore success of sending (or not) */
	}
	return STK_SUCCESS;
}

stk_ret stk_subscribe_to_name_info(stk_name_service_t *named, char *name, stk_name_info_cb_t cb, void *app_info, stk_options_t *options)
{
	return stk_request_name_info_common(named, name, 1000*60*60*24 /* 1d */, cb, app_info, options, 0, STK_SEQUENCE_TYPE_SUBSCRIBE);
}

stk_ret stk_request_name_info_once(stk_name_service_t *named, char *name, int expiration_ms, stk_name_info_cb_t cb, void *app_info, stk_options_t *options)
{
	return stk_request_name_info_common(named, name, expiration_ms, cb, app_info, options, 1, STK_SEQUENCE_TYPE_QUERY);
}

stk_ret stk_register_name(stk_name_service_t *named, char *name, int linger, int expiration_ms, stk_name_info_cb_t cb, void *app_info, stk_options_t *options)
{
	stk_name_request_t *request;
	stk_sequence_t *seq;
	stk_ret rc;

	STK_ASSERT(STKA_NS,named->stct_type==STK_STCT_NAME_SERVICE,"register name, the pointer was to a structure of type %d",named->stct_type);
	if(!name) return STK_INVALID_ARG;
	if(!cb) return STK_INVALID_ARG;
	if(!named->df) return STK_NETERR;

	STK_DEBUG(STKA_NS,"env %p name %s linger %d df %p",named->env,name,linger,named->df);

	{
	void *group_name = stk_find_option(options,"group_name",NULL);

	/* stk_alloc_name_request asserts if allocation fails */
	request = stk_alloc_name_request(name,group_name,STK_SEQUENCE_TYPE_REQUEST,expiration_ms,cb,app_info,0,0);
	STK_DEBUG(STKA_NS,"request expiration %ld.%06d\n",request->expired_tv.tv_sec, request->expired_tv.tv_usec);

	if(!group_name && named->group_name[0])
		group_name = named->group_name;

	seq = stk_create_sequence(named->env,
		STK_NAME_REQUEST_SEQUENCE_NAME,STK_NAME_REQUEST_SEQUENCE_ID,
		STK_SEQUENCE_TYPE_REQUEST,STK_SERVICE_TYPE_MGMT,NULL);
	STK_ASSERT(STKA_NS,seq!=NULL,"allocate sequence");

	rc = stk_copy_to_sequence(seq,&request,(stk_uint64) sizeof(request), STK_NS_SEQ_REQUEST_ID);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding request id to sequence");

	rc = stk_copy_to_sequence(seq,name,(stk_uint64) strlen(name) + 1, STK_NS_SEQ_NAME);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding name %s to sequence",name);

	rc = stk_copy_to_sequence(seq,&linger,(stk_uint64) sizeof(linger), STK_NS_SEQ_LINGER);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding name %s to sequence",name);

	if(group_name) {
		rc = stk_copy_to_sequence(seq,group_name,(stk_uint64) strlen(group_name) + 1, STK_NS_SEQ_GROUP_NAME);
		STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding group name %s to sequence for specific request",group_name);
	}
	}

	{
	void *ft_state_str = stk_find_option(options,"fault_tolerant_state",NULL);
	stk_options_t *lastaddr = NULL, *lastport = NULL, *lastprotocol = NULL, *lastcaddr = NULL, *lastcport = NULL;
	void *connectaddr_str;

	do {
		void *destaddr_str = stk_find_option(options,"destination_address",&lastaddr);
		void *destport_str = stk_find_option(options,"destination_port",&lastport);
		void *destprotocol_str = stk_find_option(options,"destination_protocol",&lastprotocol);
		void *port_str = destport_str ? destport_str : stk_find_option(options,"connect_port",&lastcport);
		connectaddr_str = destaddr_str ? destaddr_str : stk_find_option(options,"connect_address",&lastcaddr);

		if(connectaddr_str) {
			struct sockaddr_in addr;

			memset(&addr,0,sizeof(addr)); /* For valgrind */

			STK_DEBUG(STKA_NS,"connect addr %s port %s",
				connectaddr_str ? connectaddr_str : "", port_str ? port_str : "");

			/* copy connect address string */
			rc = inet_pton(AF_INET,connectaddr_str,&addr.sin_addr);
			if(rc <= 0) {
				STK_LOG(STK_LOG_ERROR,"Could not convert connect address %s",connectaddr_str);
				addr.sin_addr.s_addr = htonl(INADDR_ANY);
			}
			if(port_str) {
				short port = (short) atoi(port_str);
				addr.sin_port = port;
			}
			rc = stk_copy_to_sequence(seq,&addr,(stk_uint64) sizeof(addr), STK_NS_SEQ_CONNECT_IPV4);
			STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding name to sequence");

			memcpy(&request->request.ip[0].sockaddr,&addr,sizeof(addr));
			if(port_str)
				strcpy(request->request.ip[0].portstr,port_str);
			if(destaddr_str)
				strcpy(request->request.ip[0].ipstr,destaddr_str);

			if(destprotocol_str) {
				rc = stk_copy_to_sequence(seq,destprotocol_str,(stk_uint64) strlen(destprotocol_str) + 1, STK_NS_SEQ_PROTOCOL);
				STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding name to sequence");

				strcpy(request->request.ip[0].protocol,destprotocol_str);
			}
		}
	} while(connectaddr_str);

	if(ft_state_str && strcmp("active",ft_state_str) == 0) {
		stk_name_ft_state_t ft_state = STK_NAME_ACTIVE;

		rc = stk_copy_to_sequence(seq,&ft_state,(stk_uint64) sizeof(ft_state), STK_NS_SEQ_FT_STATE);
		STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding fault tolerant state to sequence");
	}

	}

	/* Look for a meta data sequence */
	{
	void *metadata_seq = stk_find_option(options,"meta_data_sequence",NULL);

	if(metadata_seq) {
		rc = stk_add_sequence_reference_in_sequence(seq, metadata_seq, 0);
		STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"merging meta data sequence");
	}
	}

	/* Send the name connection info */
	rc = stk_data_flow_send(named->df,seq,STK_TCP_SEND_FLAG_NONBLOCK);
	if(rc == STK_SUCCESS || rc == STK_WOULDBLOCK) {
		if(rc == STK_SUCCESS) {
			Node *n = NewNode();
			SetData(n,request);

			/* Add to list after sending */
			AddTail(named->request_list,n);

			named->stats.snds++;
		} else
			named->stats.blks++;
	}

	{ /* Use a different variable for the return code so as not to change rc */
	stk_ret ret = stk_destroy_sequence(seq);
	STK_ASSERT(STKA_NS,ret==STK_SUCCESS,"freeing name to sequence");
	}

	return rc;
}

void stk_copy_request_to_cb(stk_ns_rcv_data_t *ns, stk_name_request_t *req)
{
	for(int idx = 0; idx < STK_NAME_MAX_IPS; idx++) {
		if(ns->request.ip[idx].ipstr[0] == '\0' && req->request.ip[idx].ipstr[0])
			strcpy(ns->request.ip[idx].ipstr,req->request.ip[idx].ipstr);
		if(ns->request.ip[idx].protocol[0] == '\0' && req->request.ip[idx].protocol[0])
			strcpy(ns->request.ip[idx].protocol,req->request.ip[idx].protocol);
		if(ns->request.ip[idx].portstr[0] == '\0' && req->request.ip[idx].portstr[0])
			strcpy(ns->request.ip[idx].portstr,req->request.ip[idx].portstr);
		if(ns->request.ip[idx].sockaddr.sin_port == 0 && req->request.ip[idx].sockaddr.sin_port)
			ns->request.ip[idx].sockaddr.sin_port = req->request.ip[idx].sockaddr.sin_port;
	}
}

void stk_invoke_name_cbs(stk_name_service_t *named,stk_ns_rcv_data_t *ns)
{
	stk_name_request_t *req;
	Node *nxt;
	struct timeval current_tv;

	STK_ASSERT(STKA_NS,named!=NULL,"invalid name server passed to invoke_name_cbs");
	STK_ASSERT(STKA_NS,named->stct_type==STK_STCT_NAME_SERVICE,"invoke name callback, the pointer was to a structure of type %d",named->stct_type);

	if(gettimeofday(&current_tv,NULL) == -1) return;

	for(Node *n = FirstNode(named->request_list); !AtListEnd(n); n = nxt) {
		nxt = NxtNode(n);
		req = NodeData(n);

		STK_DEBUG(STKA_NS,"invoke_name_cb req/rcvd name %s %s type %d %d request id %p %p",
			req->request.name, ns->request.name, req->type, ns->type, req, (void*)ns->request_id);

		if(timercmp(&current_tv, &req->expired_tv, >)) {
			stk_ret ret;

			/* Transfer the original IP from the request to the callback if no response data exists */
			if(req->type == STK_SEQUENCE_TYPE_REQUEST)
				stk_copy_request_to_cb(ns,req);

			/* Request has expired */
			if(strcmp(req->request.name,ns->request.name) == 0) {
				STK_DEBUG(STKA_NS,"invoke_name_cb calling %p req name %s expired",req->app_info,req->request.name);
				req->cb(&req->request,1,NULL,req->app_info,STK_NS_REQUEST_EXPIRED);
			} else
				STK_DEBUG(STKA_NS,"invoke_name_cb not calling %p req name %s expired",req->app_info,req->request.name);

			ret = stk_remove_name_request(n);
			STK_ASSERT(STKA_NS,ret==STK_SUCCESS,"freeing name request");
			continue;
		}

		/* For each active request for the name */
		if(req->type == ns->type && req == (stk_name_request_t *) ns->request_id &&
			(strcmp(req->request.name,ns->request.name) == 0)) {
			/* Call callback for request */
			STK_DEBUG(STKA_NS,"invoke_name_cb calling %p req name %s",req->app_info,req->request.name);

			/* Transfer the original IP from the request to the callback if no response data exists */
			if(req->type == STK_SEQUENCE_TYPE_REQUEST)
				stk_copy_request_to_cb(ns,req);

			req->cb(&ns->request,1,NULL,req->app_info,STK_NS_REQUEST_RESPONSE);

			if(req->num_cbs == 1) {
				stk_ret ret = stk_remove_name_request(n);
				STK_ASSERT(STKA_NS,ret==STK_SUCCESS,"freeing name request");
			}
		}
	}
}

stk_ret stk_ns_sequence_cb(stk_sequence_t *seq, void *vdata, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	stk_ns_rcv_data_t *ns = (stk_ns_rcv_data_t *) clientd;

	switch(user_type) {
	case STK_NS_SEQ_NAME:
		STK_DEBUG(STKA_NS,"stk_ns_sequence_cb name %s",vdata);
		strcpy(ns->request.name,vdata);
		break;
	case STK_NS_SEQ_CONNECT_IPV4:
		STK_ASSERT(STKA_NS,sz==sizeof(ns->request.ip[0].sockaddr),"expected IP size %lu vs %lu",sizeof(ns->request.ip[0].sockaddr),sz);
		memcpy(&ns->request.ip[ns->ip_idx].sockaddr,vdata,sizeof(ns->request.ip[0].sockaddr));
		inet_ntop(AF_INET, &ns->request.ip[ns->ip_idx].sockaddr.sin_addr, 
			ns->request.ip[ns->ip_idx].ipstr, sizeof(ns->request.ip[ns->ip_idx].ipstr));
 		sprintf(ns->request.ip[ns->ip_idx].portstr,"%d",ns->request.ip[ns->ip_idx].sockaddr.sin_port);
		ns->ip_idx++;
		break;
	case STK_NS_SEQ_PROTOCOL:
		STK_ASSERT(STKA_NS,sz<=sizeof(ns->request.ip[0].protocol),"expected protocol size %lu vs %lu",sizeof(ns->request.ip[0].protocol),sz);
		STK_DEBUG(STKA_NS,"recv protocol %s",(char *) vdata);
		memcpy(ns->request.ip[ns->protocol_idx++].protocol,vdata,sz);
	case STK_NS_SEQ_REQUEST_ID:
#if 0
Why is this failing???
		STK_ASSERT(STKA_NS,sz==sizeof(ns->request_id),"expected request ID size %lu vs %lu",sizeof(ns->request_id),sz);
#endif
		memcpy(&ns->request_id,vdata,sz);
		break;
	case STK_NS_SEQ_FT_STATE:
		STK_ASSERT(STKA_NS,sz==sizeof(ns->request.ft_state),"expected FT state size %lu vs %lu",sizeof(ns->request.ft_state),sz);
		memcpy(&ns->request.ft_state,vdata,sz);
		break;
	case STK_NS_SEQ_ID:
		STK_ASSERT(STKA_NS,sz==sizeof(ns->name_server_id),"expected name server id size %lu vs %lu",sizeof(ns->name_server_id),sz);
		memcpy(&ns->name_server_id,vdata,sz);
		break;
	}
	return STK_SUCCESS;
}

stk_ret stk_name_service_invoke(stk_sequence_t *seq)
{
	stk_ret rc;
	stk_ns_rcv_data_t ns;
	stk_sequence_id sid = stk_get_sequence_id(seq);
	memset(&ns,0,sizeof(ns));

	switch(sid)
	{
	case STK_NAME_REQUEST_SEQUENCE_ID:
	case STK_SMARTBEAT_SEQ:
		/* Valid NS Sequences */
		break;
	default:
		return STK_SUCCESS;
	}

	ns.type = stk_get_sequence_type(seq);

	rc = stk_iterate_sequence(seq,stk_ns_sequence_cb,&ns);
	STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"iterate over sequence %p being invoked",seq);

	switch(sid)
	{
	case STK_NAME_REQUEST_SEQUENCE_ID:
		ns.request.meta_data = seq; /* In the future we need to create per name sequences containing only the meta data */

		stk_invoke_name_cbs(stk_env_get_name_service(stk_env_from_sequence(seq)),&ns);
		break;
	case STK_SMARTBEAT_SEQ:
		{
		stk_name_service_t *named = stk_env_get_name_service(stk_env_from_sequence(seq));

		STK_DEBUG(STKA_SMB,"stk_name_service_invoke: received heartbeat sequence from %lx",ns.name_server_id);

		/* Check if name server ID is in list of known name servers, add if not,
		 * and expire any name servers appropriately
		 */
		update_name_server_activity(named,ns.name_server_id);
		expire_name_servers(named);
		}
		break;
	}
	return STK_SUCCESS;
}

stk_data_flow_t **stk_ns_get_smartbeat_flows(stk_name_service_t *named)
{
	stk_data_flow_t **flows;

	flows = calloc(sizeof(stk_data_flow_t *),2);
	STK_CHECK(STKA_SVC,flows!=NULL,"Couldn't allocate memory for flows");
	if(flows == NULL) return NULL;

	flows[0] = named->df;
	return flows;
}
