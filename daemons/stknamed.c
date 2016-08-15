
#define _GNU_SOURCE

#include <stdio.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>

#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_sequence_api.h"
#include "stk_data_flow.h"
#include "stk_smartbeat_api.h"
#include "stk_tcp_server_api.h"
#include "stk_udp_listener_api.h"
#include "stk_tcp_client_api.h"
#include "stk_tcp.h"
#include "stk_data_flow_api.h"
#include "stk_timer_api.h"
#include "stk_sync_api.h"
#include "stk_name_service.h"				/* Shared structures!!! */
#include "stk_ids.h"
#include "stk_ports.h"
#include "PLists.h"
#include "../examples/eg_dispatcher_api.h"

/* Use internal header for asserts */
#include "stk_internal.h"
#include "stk_sync.h"
#include "stk_name_store.h"
#include "stk_subscription_store.h"
#include "stk_sequence_api.h"

typedef struct cmdipopts {
	char *ip;
	char *port;
} ipopts;

/* command line options provided - set in process_cmdline() */
static struct cmdopts {
	ipopts multicast;
	ipopts unicast;
	ipopts tcp;
} opts;

extern char content[];

stk_timer_set_t *named_timers;
stk_timer_t *request_gc_timer;
stk_dispatcher_t *named_dispatcher;
static stk_uint64 named_id; /* Random ID generated on start */

void stknamed_term(int signum)
{
	printf("stknamed received SIGTERM/SIGINT, exiting...\n");
	stop_dispatching(named_dispatcher);
}

/* Query Handling */
void send_query_response(stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq,stk_uint64 *rcv_req_id,stk_name_request_data_t *request_data)
{
	stk_env_t *env = stk_env_from_data_flow(rcvchannel);
	stk_ret rc;

	/* FIXME: Removing the request id makes this a thread safety/performance issue - consider locking/duplicating sequence */
	stk_remove_sequence_data_by_type(request_data->meta_data, STK_NS_SEQ_REQUEST_ID, 1);
	rc = stk_copy_to_sequence(request_data->meta_data,rcv_req_id,(stk_uint64) sizeof(rcv_req_id), STK_NS_SEQ_REQUEST_ID);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding request id %lx to sequence",request_data->request_id);

	if(stk_get_data_flow_type(rcvchannel) == STK_UDP_CLIENT_FLOW || stk_get_data_flow_type(rcvchannel) == STK_UDP_LISTENER_FLOW)
	{
		/* This blows because for UDP the client IP in the meta_data sequence is the original
		 * requesters but we want to send to the IP of the received message. To do this we
		 * must create a new merged sequence
		 */
		stk_sequence_t *seq;
		struct sockaddr_in client_ip;
		socklen_t addrlen;

		seq = stk_create_sequence(env,
			stk_get_sequence_name(rcv_seq),stk_get_sequence_id(rcv_seq),
			stk_get_sequence_type(rcv_seq),STK_SERVICE_TYPE_MGMT,NULL);
		STK_ASSERT(STKA_SEQ,seq!=NULL,"allocate response sequence");

		rc = stk_data_flow_client_ip(rcv_seq,&client_ip,&addrlen);
		if(rc == STK_SUCCESS) {
			rc = stk_data_flow_add_client_ip(seq,&client_ip,addrlen);
			STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"add client ip while processing request");
		}

		rc = stk_add_sequence_reference_in_sequence(seq, request_data->meta_data, 0);
		STK_ASSERT(STKA_SEQ,rc==STK_SUCCESS,"adding request sequence");

		rc = stk_data_flow_send(rcvchannel,seq,STK_TCP_SEND_FLAG_NONBLOCK);
		STK_DEBUG(STKA_NS,"sending response to query for name %s rc %d",request_data->name,rc);

		rc = stk_destroy_sequence(seq);
		STK_ASSERT(STKA_SEQ,rc==STK_SUCCESS,"freeing response sequence");
	} else {
		rc = stk_data_flow_send(rcvchannel,request_data->meta_data,STK_TCP_SEND_FLAG_NONBLOCK);
	}
}

void process_query(stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq,char *name)
{
	stk_name_store_t *store;
	Node *n = NULL;
	stk_uint64 *rcv_req_id = 0L;

	{
	stk_uint64 sz = 0;
	char *group_name;

	stk_sequence_find_data_by_type(rcv_seq,STK_NS_SEQ_GROUP_NAME,(void**) &group_name,&sz);

	if(sz == 0) store = NULL;
	else store = stk_find_name_store(group_name);

	STK_LOG(STK_LOG_NORMAL,"Received name server query for %s store '%s'",name,group_name ? group_name : "(default)");

	stk_sequence_find_data_by_type(rcv_seq,STK_NS_SEQ_REQUEST_ID,(void**) &rcv_req_id,&sz);
	}

	while((n = stk_find_name_in_store(store,name,n)))
		send_query_response(rcvchannel,rcv_seq,rcv_req_id,(stk_name_request_data_t *) NodeData(n));
}

/* Registration Handling */
stk_ret store_request(stk_name_request_data_t *request_data)
{
	stk_ret rc;
	stk_name_store_t *store;

	STK_LOG(STK_LOG_NORMAL,"Processing request for name %s store '%s'",request_data->name,request_data->group_name);

	if(request_data->group_name[0] != '\0') {
		STK_DEBUG(STKA_NS,"name %s group_name %s",request_data->name,request_data->group_name);

		store = stk_find_name_store(request_data->group_name);
		if(store == stk_default_name_store())
			store = stk_add_name_store(request_data->group_name);
	}
	else
		store = NULL;

	{ /* remove duplicate entries from this data flow */
	Node *n = NULL;

	while((n = stk_find_name_in_store(store,request_data->name,n))) {
		stk_name_request_data_t *old_request_data = NodeData(n);
		if(old_request_data->df == request_data->df)
			break;
	}

	if(n) {
		rc = stk_remove_node_from_name_store(store,n,NULL);
		STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"remove duplicate entry from IP/Port for name %s to name store %p",request_data->name,store);
	}
	}

	/* Store the request */
	rc = stk_add_to_name_store(store,request_data);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS || rc==STK_NO_LICENSE,"add name %s to name store %p rc %d",request_data->name,store,rc);

	return rc;
}

stk_ret process_request_data(stk_sequence_t *seq, void *vdata, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	stk_name_request_data_t *reqdata = (stk_name_request_data_t *) clientd;

	STK_DEBUG(STKA_NS,"Sequence %p Received %ld bytes of type %lx",seq,sz,user_type);
	switch(user_type) {
	case STK_NS_SEQ_NAME: break; /* Already processed */
	case STK_NS_SEQ_GROUP_NAME:
		memcpy(reqdata->group_name,vdata,sz);
		break;
	case STK_NS_SEQ_CONNECT_IPV4:
		STK_ASSERT(STKA_NS,sz==sizeof(reqdata->ipv4),"expected IP size %lu vs %lu",sizeof(reqdata->ipv4),sz);
		memcpy(&reqdata->ipv4,vdata,sizeof(reqdata->ipv4));
		break;
	case STK_NS_SEQ_REQUEST_ID:
		STK_ASSERT(STKA_NS,sz==sizeof(reqdata->request_id),"expected request ID size %lu vs %lu",sizeof(reqdata->request_id),sz);
		memcpy(&reqdata->request_id,vdata,sz);
		break;
	case STK_NS_SEQ_FT_STATE:
		STK_ASSERT(STKA_NS,sz==sizeof(reqdata->ft_state),"expected FT state size %lu vs %lu",sizeof(reqdata->ft_state),sz);
		memcpy(&reqdata->ft_state,vdata,sz);
		break;
	case STK_NS_SEQ_LINGER:
		STK_ASSERT(STKA_NS,sz==sizeof(int),"expected linger size %lu vs %lu",sizeof(reqdata->linger),sz);
		reqdata->linger = *((int *)vdata);
		break;
	}

	return STK_SUCCESS;
}

void process_request(stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq,char *name)
{
	stk_env_t *env = stk_env_from_data_flow(rcvchannel);
	stk_name_request_data_t *request_data;
	stk_ret rc;

	STK_LOG(STK_LOG_NORMAL,"Received name server request for %s",name);

	request_data = STK_CALLOC(sizeof(stk_name_request_data_t));
	STK_ASSERT(STKA_NS,request_data!=NULL,"alloc request data");

	strncpy(request_data->name,name,STK_MAX_NAME_LEN);
	request_data->name[STK_MAX_NAME_LEN - 1] = '\0';
	request_data->meta_data = rcv_seq;
	request_data->df = rcvchannel;
	stk_hold_sequence(rcv_seq);

	/* Call process_request_data() on each element in the sequence */
	rc = stk_iterate_sequence(rcv_seq,process_request_data,request_data);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"process received request data");

	{
	unsigned char *ipv4 = (unsigned char *) &request_data->ipv4.sin_addr;

	STK_LOG(STK_LOG_NORMAL,"Processing request for name %s ip %d.%d.%d.%d:%d",request_data->name,
		ipv4[0],ipv4[1],ipv4[2],ipv4[3], request_data->ipv4.sin_port);
	}

	/* convert sequence stored to be a query ready to be sent in response to queries */
	rc = stk_set_sequence_type(rcv_seq,STK_SEQUENCE_TYPE_QUERY);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"convert sequence to query");

	/* Consume the request, send confirmation back if successful */
	if(store_request(request_data) == STK_SUCCESS)
	{
		{
		stk_sequence_t *seq;
		struct sockaddr_in client_ip;
		socklen_t addrlen;

		seq = stk_create_sequence(env,
			STK_NAME_REQUEST_SEQUENCE_NAME,STK_NAME_REQUEST_SEQUENCE_ID,
			STK_SEQUENCE_TYPE_REQUEST,STK_SERVICE_TYPE_MGMT,NULL);
		STK_ASSERT(STKA_SEQ,seq!=NULL,"allocate response sequence");

		rc = stk_copy_to_sequence(seq,&request_data->request_id,(stk_uint64) sizeof(request_data->request_id), STK_NS_SEQ_REQUEST_ID);
		STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding request id %lx to sequence",request_data->request_id);

		rc = stk_copy_to_sequence(seq,request_data->name,(stk_uint64) strlen(request_data->name) + 1, STK_NS_SEQ_NAME);
		STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding name %s to sequence",name);

		rc = stk_data_flow_client_ip(rcv_seq,&client_ip,&addrlen);
		if(rc == STK_SUCCESS) {
			rc = stk_data_flow_add_client_ip(seq,&client_ip,addrlen);
			STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"add client ip while processing request");
		}

		rc = stk_data_flow_send(rcvchannel,seq,STK_TCP_SEND_FLAG_NONBLOCK);
		if(rc != STK_SUCCESS)
			STK_LOG(STK_LOG_NORMAL,"Failed to send response on data flow %p rc %d",rcvchannel,rc);

		STK_DEBUG(STKA_NS,"send response on data flow %p rc %d",rcvchannel,rc);

		rc = stk_destroy_sequence(seq);
		STK_ASSERT(STKA_SEQ,rc==STK_SUCCESS,"freeing response sequence");
		}

		{ /* Notify existing subscriptions */
		stk_subscription_store_t *store;
		Node *n = NULL;
		stk_uint64 rcv_req_id;

		{
		stk_uint64 sz = 0;
		char *group_name;

		stk_sequence_find_data_by_type(rcv_seq,STK_NS_SEQ_GROUP_NAME,(void**) &group_name,&sz);

		if(sz == 0) store = NULL;
		else store = stk_find_subscription_store(group_name);

		STK_LOG(STK_LOG_NORMAL,"Received name server query for %s store '%s'",name,group_name ? group_name : "(default)");

		}

		while((n = stk_find_subscription_in_store(store,name,n))) {
			stk_data_flow_t *df = stk_get_subscription_data_flow(NodeData(n));
			if(df) {
				rcv_req_id = stk_get_subscription_id(NodeData(n));
				send_query_response(df,rcv_seq,&rcv_req_id,request_data);
			}
		}
		}
	}
}

/* Registration expiration handling */
stk_ret stk_request_gc_store_cb(stk_name_store_t *store,void *clientd)
{
	struct timeval *current_tv = (struct timeval *)clientd;
	Node *n = NULL;
	stk_ret rc;

	while((n = stk_find_expired_names_in_store(store,current_tv,n))) {
		stk_name_request_data_t *request_data = (stk_name_request_data_t *) NodeData(n);
		Node *nxt = stk_next_name_in_store(store,n);

		STK_DEBUG(STKA_NS,"expiring name %s",request_data->name);

		/* Remove node and free */
		rc = stk_remove_node_from_name_store(store,n,NULL);
		if(rc != STK_SUCCESS) return rc;

		/* Hmm - is this leaking stores?? See stk_name_store.c stk_name_store_cleanup() */

		n = nxt;
	}
	return STK_SUCCESS;
}

/* Subscription expiration handling */
stk_ret stk_request_gc_subscription_store_cb(stk_subscription_store_t *store,void *clientd)
{
	struct timeval *current_tv = (struct timeval *)clientd;
	Node *n = NULL;
	stk_ret rc;

	while((n = stk_find_expired_subscriptions_in_store(store,current_tv,n))) {
		stk_name_subscription_data_t *request_data = (stk_name_subscription_data_t *) NodeData(n);
		Node *nxt = stk_next_subscription_in_store(store,n);

		STK_DEBUG(STKA_NS,"expiring subscription %s",request_data->name);

		/* Remove node and free */
		rc = stk_remove_node_from_subscription_store(store,n,NULL);
		if(rc != STK_SUCCESS) return rc;

		/* Hmm - is this leaking subscriptions?? See above for name store leaks */

		n = nxt;
	}
	return STK_SUCCESS;
}

void stk_request_gc_cb(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type)
{
	stk_env_t *env = stk_env_from_timer_set(timer_set);
	struct timeval current_tv;
	stk_ret rc;

	if(cb_type == STK_TIMER_CANCELLED) return;

	rc = stk_reschedule_timer(timer_set,timer);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"reschedule request gc timer for name service");

	if(gettimeofday(&current_tv,NULL) == -1)
		return;

	stk_iterate_name_stores(stk_request_gc_store_cb,&current_tv);
	stk_iterate_subscription_stores(stk_request_gc_subscription_store_cb,&current_tv);
}


/* Subscription Handling */
void store_subscription(stk_name_subscription_data_t *subscription_data)
{
	stk_ret rc;
	stk_subscription_store_t *store;

	STK_LOG(STK_LOG_NORMAL,"Processing subscription for name %s store '%s'",subscription_data->name,subscription_data->group_name);

	if(subscription_data->group_name[0] != '\0') {
		STK_DEBUG(STKA_NS,"name %s group_name %s",subscription_data->name,subscription_data->group_name);

		store = stk_find_subscription_store(subscription_data->group_name);
		if(store == stk_default_subscription_store())
			store = stk_add_subscription_store(subscription_data->group_name);
	}
	else
		store = NULL;

	{ /* remove duplicate entries from this data flow */
	Node *n = NULL;

	while((n = stk_find_subscription_in_store(store,subscription_data->name,n))) {
		stk_name_subscription_data_t *old_subscription_data = NodeData(n);
		if(old_subscription_data->df == subscription_data->df)
			break;
	}

	if(n) {
		rc = stk_remove_node_from_subscription_store(store,n,NULL);
		STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"remove duplicate entry from IP/Port for name %s to name store %p",subscription_data->name,store);
	}
	}

	/* Store the subscription */
	rc = stk_add_to_subscription_store(store,subscription_data);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"add name %s to name store %p",subscription_data->name,store);

	return;
}

stk_ret process_subscription_data(stk_sequence_t *seq, void *vdata, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	stk_name_subscription_data_t *reqdata = (stk_name_subscription_data_t *) clientd;

	STK_DEBUG(STKA_NS,"Sequence %p Received %ld bytes of type %lx",seq,sz,user_type);
	switch(user_type) {
	case STK_NS_SEQ_NAME: break; /* Already processed */
	case STK_NS_SEQ_GROUP_NAME:
		memcpy(reqdata->group_name,vdata,sz);
		break;
	case STK_NS_SEQ_REQUEST_ID:
		STK_ASSERT(STKA_NS,sz==sizeof(reqdata->subscription_id),"expected subscription ID size %lu vs %lu",sizeof(reqdata->subscription_id),sz);
		memcpy(&reqdata->subscription_id,vdata,sz);
		break;
	}

	return STK_SUCCESS;
}

void add_subscription(stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq,char *name)
{
	stk_env_t *env = stk_env_from_data_flow(rcvchannel);
	stk_name_subscription_data_t *subscription_data;
	stk_ret rc;

	STK_LOG(STK_LOG_NORMAL,"Received name server subscription for %s",name);

	subscription_data = STK_CALLOC(sizeof(stk_name_subscription_data_t));
	STK_ASSERT(STKA_NS,subscription_data!=NULL,"alloc subscription data");

	strncpy(subscription_data->name,name,STK_MAX_NAME_LEN);
	subscription_data->name[STK_MAX_NAME_LEN - 1] = '\0';
	subscription_data->df = rcvchannel;
	stk_hold_sequence(rcv_seq);

	/* Call process_subscription_data() on each element in the sequence */
	rc = stk_iterate_sequence(rcv_seq,process_subscription_data,subscription_data);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"process received subscription data");

	/* convert sequence stored to be a query ready to be sent in response to queries */
	rc = stk_set_sequence_type(rcv_seq,STK_SEQUENCE_TYPE_QUERY);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"convert sequence to query");

	/* Consume the subscription */
	store_subscription(subscription_data);

	/* Send confirmation back? */
	{
	stk_sequence_t *seq;
	struct sockaddr_in client_ip;
	socklen_t addrlen;

	seq = stk_create_sequence(env,
		STK_NAME_REQUEST_SEQUENCE_NAME,STK_NAME_REQUEST_SEQUENCE_ID,
		STK_SEQUENCE_TYPE_REQUEST,STK_SERVICE_TYPE_MGMT,NULL);
	STK_ASSERT(STKA_SEQ,seq!=NULL,"allocate response sequence");

	rc = stk_copy_to_sequence(seq,&subscription_data->subscription_id,(stk_uint64) sizeof(subscription_data->subscription_id), STK_NS_SEQ_REQUEST_ID);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding subscription id %lx to sequence",subscription_data->subscription_id);

	rc = stk_copy_to_sequence(seq,subscription_data->name,(stk_uint64) strlen(subscription_data->name) + 1, STK_NS_SEQ_NAME);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding name %s to sequence",name);

	rc = stk_data_flow_client_ip(rcv_seq,&client_ip,&addrlen);
	if(rc == STK_SUCCESS) {
		rc = stk_data_flow_add_client_ip(seq,&client_ip,addrlen);
		STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"add client ip while processing subscription");
	}

	rc = stk_data_flow_send(rcvchannel,seq,STK_TCP_SEND_FLAG_NONBLOCK);
	if(rc != STK_SUCCESS)
		STK_LOG(STK_LOG_NORMAL,"Failed to send response on data flow %p rc %d",rcvchannel,rc);

	STK_DEBUG(STKA_NS,"send response on data flow %p rc %d",rcvchannel,rc);

	rc = stk_destroy_sequence(seq);
	STK_ASSERT(STKA_SEQ,rc==STK_SUCCESS,"freeing response sequence");

/* Should subscriptions initiate a smart beat back to originators? Optional? */
	}
}

void process_subscription(stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq,char *name)
{
	/* Add subscription to list for future registrations */
	add_subscription(rcvchannel,rcv_seq,name);

	/* Process as a query to send back immediate responses */
	process_query(rcvchannel,rcv_seq,name);

	return;
}

/* General Message Handling */
stk_smartbeat_t daemon_smb;
static void process_data(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc;
	char *name;

	rc = stk_smartbeat_update_current_time(&daemon_smb);
	STK_ASSERT(STKA_NS,rc == STK_SUCCESS,"get current time");

	if(stk_get_sequence_id(rcv_seq) == STK_SMARTBEAT_SEQ) {
		/* send back the heartbeat to the sender */
		STK_DEBUG(STKA_SMB,"received heartbeat, responding");

		rc = stk_copy_to_sequence(rcv_seq,&named_id,(stk_uint64) sizeof(named_id), STK_NS_SEQ_ID);
		STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"adding name ID to smartbeat");

		rc = stk_data_flow_send(rcvchannel,rcv_seq,STK_TCP_SEND_FLAG_NONBLOCK);
		if(rc != STK_SUCCESS)
			STK_DEBUG(STKA_SMB,"Failed to return smartbear to client.");

		return;
	}

	{
	stk_uint64 sz = 0;
	stk_sequence_find_data_by_type(rcv_seq,STK_NS_SEQ_NAME,(void**) &name,&sz);
	}

	switch(stk_get_sequence_type(rcv_seq)) {
	case STK_SEQUENCE_TYPE_REQUEST:
		process_request(rcvchannel,rcv_seq,name);
		return;
	case STK_SEQUENCE_TYPE_QUERY:
		process_query(rcvchannel,rcv_seq,name);
		return;
	case STK_SEQUENCE_TYPE_SUBSCRIBE:
		process_subscription(rcvchannel,rcv_seq,name);
		return;
	default:
		STK_LOG(STK_LOG_NORMAL,"Received unexpected sequence type %d",stk_get_sequence_type(rcv_seq));
		return;
	}
}

typedef struct {
	stk_data_flow_t *df;
	struct timeval *current_tv;
} stk_destroyed_clientd_t;

stk_ret stk_df_destroyed_store_cb(stk_name_store_t *store,void *clientd)
{
	stk_destroyed_clientd_t *cbdata = (stk_destroyed_clientd_t *) clientd;
	Node *n = NULL;

	STK_DEBUG(STKA_NS,"stk_df_destroyed_store_cb store %p df %p",store,cbdata->df);
	while((n = stk_find_names_from_df_in_store(store,cbdata->df,n))) {
		stk_name_request_data_t *request_data = (stk_name_request_data_t *) NodeData(n);
		Node *nxt = stk_next_name_in_store(store,n);

		memcpy(&request_data->expiration_tv,cbdata->current_tv,sizeof(*cbdata->current_tv));
		request_data->expiration_tv.tv_sec += request_data->linger;
		request_data->df = NULL;

		STK_DEBUG(STKA_NS,"lingering %d name %s [%ld.%06d] current [%ld.%06d] n %p nxt %p",request_data->linger, request_data->name,
			request_data->expiration_tv.tv_sec, request_data->expiration_tv.tv_usec, cbdata->current_tv->tv_sec, cbdata->current_tv->tv_usec,
			n,nxt);

		if(!nxt) break;

		n = nxt;
	}
	return STK_SUCCESS;
}

stk_ret stk_df_destroyed_subscription_store_cb(stk_subscription_store_t *store,void *clientd)
{
	stk_destroyed_clientd_t *cbdata = (stk_destroyed_clientd_t *) clientd;
	Node *n = NULL;

	STK_DEBUG(STKA_NS,"stk_df_destroyed_store_cb store %p df %p",store,cbdata->df);
	while((n = stk_find_subscriptions_from_df_in_store(store,cbdata->df,n))) {
		stk_name_subscription_data_t *request_data = (stk_name_subscription_data_t *) NodeData(n);
		Node *nxt = stk_next_subscription_in_store(store,n);

		memcpy(&request_data->expiration_tv,cbdata->current_tv,sizeof(*cbdata->current_tv));
		request_data->df = NULL;

		STK_DEBUG(STKA_NS,"subscription %s [%ld.%06d] current [%ld.%06d] n %p nxt %p", request_data->name,
			request_data->expiration_tv.tv_sec, request_data->expiration_tv.tv_usec, cbdata->current_tv->tv_sec, cbdata->current_tv->tv_usec,
			n,nxt);

		if(!nxt) break;

		n = nxt;
	}
	return STK_SUCCESS;
}

void stkn_data_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	if (stk_get_data_flow_type(flow) == STK_TCP_ACCEPTED_FLOW) {
		int added = dispatch_add_accepted_fd(named_dispatcher,fd,flow,process_data);
		STK_ASSERT(STKA_NS,added != -1,"add accepted data flow (fd %d) to dispatcher",fd);
	}
}

void stk_df_destroyed_cb(stk_data_flow_t *df,stk_data_flow_id id)
{
	struct timeval current_tv;
	stk_destroyed_clientd_t data = { df, &current_tv };

	/* Update requests that came from this data flow to start them lingering */
	if(gettimeofday(&current_tv,NULL) == -1)
		return;

	stk_iterate_name_stores(stk_df_destroyed_store_cb,&data);
	stk_iterate_subscription_stores(stk_df_destroyed_subscription_store_cb,&data);
}

static void process_cmdipopts(ipopts *opts,char *default_ip,char *default_port)
{
	char *colon;

	if(!strcmp(optarg,"-")) {
		opts->ip = default_ip;
		opts->port = default_port;
	} else {
		opts->ip = optarg;
		colon = strchr(optarg,':');
		if(colon) {
			*colon = '\0';
			opts->port = ++colon;
		}
	}
}
 
void stk_named_usage()
{
	fprintf(stderr,"Usage: stknamed [options]\n");
	fprintf(stderr,"       -h                     : This help!\n");
	fprintf(stderr,"       -M <-|ip[:port]>       : Multicast Listening IP and port (default 224.10.10.20:20002)\n");
	fprintf(stderr,"       -U <-|ip[:port]>       : UDP [unicast] Listening IP and port (default 127.0.0.1:20002)\n");
	fprintf(stderr,"       -T <-|ip[:port]>       : TCP Listening IP and port (default 127.0.0.1:20002)\n");
}

static int process_cmdline(int argc,char *argv[],struct cmdopts *opts )
{
	int rc;

	while(1) {
		rc = getopt(argc, argv, "hP:M:U:T:");
		if(rc == -1) return 0;

		switch(rc) {
		case 'h': /* Help! */
			stk_named_usage();
			exit(0);

		case 'M': /* IP/Port of multicast name server*/
			process_cmdipopts(&opts->multicast,"224.10.10.20","20002");
			break;

		case 'U': /* IP/Port of unicast name server */
			process_cmdipopts(&opts->unicast,"127.0.0.1","20002");
			break;

		case 'T': /* IP/Port of tcp name server */
			process_cmdipopts(&opts->tcp,"127.0.0.1","20002");
			break;
		}
	}
	return 0;
}

static int seed;
static stk_uint64 stk_generate_unique_id()
{
	if(seed == 0) {
		struct timeval tv;

		gettimeofday(&tv,NULL);
		srand((unsigned int) (tv.tv_sec + tv.tv_usec)); 
		seed = 1;
	}
	return (stk_uint64) (rand()<<((sizeof(int)*8)-1)|rand());
}

int stk_named_main(int shared,int argc,char *argv[])
{
	stk_env_t *stkbase;
	stk_bool rc;
	int defbufsize = 500;

	/* Get the command line options and fill out opts with user choices */
	if(process_cmdline(argc,argv,&opts) == -1) {
		stk_named_usage();
		exit(5);
	}

	named_dispatcher = alloc_dispatcher();

	if(!shared) {
		signal(SIGTERM, stknamed_term); /* kill */
		signal(SIGINT, stknamed_term);  /* ctrl-c */
	}

	{
	stk_options_t options[] = { { "inhibit_name_service", (void *)STK_TRUE}, { NULL, NULL } };

	stkbase = stk_create_env(options);
	STK_ASSERT(STKA_NS,stkbase!=NULL,"allocate an stk environment");
	}

	named_id = stk_generate_unique_id();
	STK_LOG(STK_LOG_NORMAL,"named id: %lx",named_id);

	rc = stk_name_store_init();
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"create name store");

	rc = stk_subscription_store_init();
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"create subscription store");

	named_timers = stk_new_timer_set(stkbase,NULL,10,STK_TRUE);

	request_gc_timer = stk_schedule_timer(named_timers,stk_request_gc_cb,0,NULL,1000);

	{
	stk_data_flow_t *tcp_df = NULL;
	stk_data_flow_t *udp_df = NULL;
	stk_data_flow_t *mcast_df = NULL;

	if(opts.multicast.ip)
	{
		stk_options_t udp_options[] = { { "bind_address", "0.0.0.0"}, {"bind_port", int_to_string_p(STK_NAMED_RCV_DF_PORT)}, {"reuseaddr", (void*) STK_TRUE},
			{ "receive_buffer_size", "16000000" }, { "multicast_address", "224.10.10.20" }, { NULL, NULL } };
		int irc;

		/* Override defaults if configured */
		if(opts.multicast.ip) udp_options[4].data = opts.multicast.ip;
		if(opts.multicast.port) udp_options[1].data = opts.multicast.port;

		mcast_df = stk_udp_listener_create_data_flow(stkbase,"udp multicast socket for stknamed", STK_EG_SERVER_DATA_FLOW_ID, udp_options);
		STK_ASSERT(STKA_HTTPD,mcast_df!=NULL,"Failed to create multicast udp listener data flow");

		irc = dispatch_add_fd(named_dispatcher,mcast_df,stk_udp_listener_fd(mcast_df),NULL,process_data);
		STK_ASSERT(STKA_HTTPD,irc>=0,"Add multicast udp listener data flow to dispatcher");
	}

	if(opts.unicast.ip)
	{
		stk_options_t udp_options[] = { { "bind_address", "0.0.0.0"}, {"bind_port", int_to_string_p(STK_NAMED_RCV_DF_PORT)}, {"reuseaddr", (void*) STK_TRUE},
			{ "receive_buffer_size", "16000000" }, { NULL, NULL } };
		int irc;

		/* Override defaults if configured */
		if(opts.unicast.ip) udp_options[0].data = opts.unicast.ip;
		if(opts.unicast.port) udp_options[1].data = opts.unicast.port;

		udp_df = stk_udp_listener_create_data_flow(stkbase,"udp socket for stknamed", STK_EG_SERVER_DATA_FLOW_ID, udp_options);
		STK_ASSERT(STKA_HTTPD,udp_df!=NULL,"Failed to create multicast udp listener data flow");

		irc = dispatch_add_fd(named_dispatcher,udp_df,stk_udp_listener_fd(udp_df),NULL,process_data);
		STK_ASSERT(STKA_HTTPD,irc>=0,"Add udp listener data flow to dispatcher");
	}

	{
	stk_data_flow_t *df;
	int irc;

	stk_options_t options[] = { { "bind_address", "0.0.0.0"}, {"bind_port", int_to_string_p(STK_NAMED_RCV_DF_PORT) }, {"reuseaddr", (void*) STK_TRUE}, {"nodelay", (void*) STK_TRUE},
	{ "receive_buffer_size", "1024000" }, { "send_buffer_size", "512000" },
	{ "fd_created_cb", (void *) stkn_data_fd_created_cb }, { "df_destroyed_cb", (void *) stk_df_destroyed_cb }, { NULL, NULL} };

	df = stk_tcp_server_create_data_flow(stkbase,"tcp server socket for data flow test",STK_NAME_SERVICE_DATA_FLOW_ID,options);
	STK_ASSERT(STKA_NS,df!=NULL,"create tcp server data flow");
	tcp_df = df;

/* Need fd_destroyed callback to clean up subscriptions?? */

	irc = server_dispatch_add_fd(named_dispatcher,stk_tcp_server_fd(tcp_df),tcp_df,process_data);
	STK_ASSERT(STKA_HTTPD,irc>=0,"Failed to add tcp server data flow to dispatcher");
	}

	eg_dispatcher(named_dispatcher,stkbase,100);

	rc = stk_free_timer_set(named_timers,STK_TRUE);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"destroy the timer set : %d",rc);

	rc = stk_destroy_data_flow(tcp_df);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"destroy the tcp data flow: %d",rc);
	if(udp_df) {
		int removed = dispatch_remove_fd(named_dispatcher,stk_udp_listener_fd(udp_df));
		STK_ASSERT(STKA_HTTPD,removed != -1,"remove udp data flow from dispatcher");
		rc = stk_destroy_data_flow(udp_df);
		STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"destroy the udp data flow: %d",rc);
	}
	if(mcast_df) {
		int removed = dispatch_remove_fd(named_dispatcher,stk_udp_listener_fd(mcast_df));
		STK_ASSERT(STKA_HTTPD,removed != -1,"remove mcast data flow from dispatcher");
		rc = stk_destroy_data_flow(mcast_df);
		STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"destroy the mcast data flow: %d",rc);
	}

	}

	stk_name_store_cleanup();

	rc = stk_destroy_env(stkbase);
	STK_ASSERT(STKA_NS,rc==STK_SUCCESS,"destroy a stk env object : %d",rc);

	return 0;
}

