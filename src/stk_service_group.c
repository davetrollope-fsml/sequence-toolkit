#include "stk_service_group_api.h"
#include "stk_service_group.h"
#include "stk_env.h"
#include "stk_service_api.h"
#include "stk_service.h"
#include "stk_options_api.h"
#include "stk_internal.h"
#include "stk_timer_api.h"
#include "stk_sync_api.h"
#include "stk_smartbeat_api.h"
#include "stk_sg_automation_api.h"
#include "PLists.h"
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#ifdef STK_LITE
#define STK_SERVICE_LIMIT 5
#endif

stk_timer_set_t *stk_svc_group_timers;
static int timer_refcount;

typedef struct stk_service_state_in_group_stct
{
	stk_stct_type stct_type;
	stk_service_in_group_state state;
	stk_service_t *service;
	struct sockaddr_in ip;
} stk_service_state_in_group_t;

struct stk_service_group_stct
{
	stk_stct_type stct_type;
	stk_env_t *env;
	char *name;
	stk_service_group_id id;
	stk_service_group_state state;
	List *svc_list; /* List of stk_service_state_in_group_t structs */
	stk_service_added_cb svc_added_cb;
	stk_service_removed_cb svc_removed_cb;
	stk_service_smartbeat_cb svc_smb_cb;
	stk_options_t *options;
	stk_timer_t *activity_timer;
};

void stk_svc_group_activity_cb(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type);
void stk_remove_service_node_from_group(stk_service_group_t *svc_group,stk_service_t *svc,Node *n,stk_service_state_in_group_t *st);

stk_service_group_t *stk_create_service_group(stk_env_t *env,char *name, stk_service_group_id id, stk_options_t *options)
{
	stk_service_group_t *svcgrp;

	if(stk_svc_group_timers == NULL) {
		stk_svc_group_timers = stk_new_timer_set(env,NULL,0,STK_TRUE);
		STK_ASSERT(STKA_NET,stk_svc_group_timers!=NULL,"allocate a timer set for service groups");
	}
	STK_ATOMIC_INCR(&timer_refcount);

	STK_CALLOC_STCT(STK_STCT_SERVICE_GROUP,stk_service_group_t,svcgrp);
	if(svcgrp) {
		stk_ret rc;

		svcgrp->name = name ? strdup(name) : NULL;
		svcgrp->env = env;
		svcgrp->id = id;
		svcgrp->state = STK_SERVICE_GROUP_INIT;
		svcgrp->svc_list = NewPList();
		if(svcgrp->svc_list == NULL) {
			STK_FREE_STCT(STK_STCT_SERVICE_GROUP,svcgrp);
			svcgrp = NULL;
			return NULL;
		}

		/* Store the option set provided so monitoring options can
		 * be passed through to the services module from automation.
		 * Also, extend to add an option so automation created
		 * services know to add the service group name to monitoring
		 * sequences.
		 * Should this code remove notification_df's from the option list? Could get recursive if badly configured?
		 */
		svcgrp->options = stk_copy_extend_options(options, 1);
		rc = stk_append_option(svcgrp->options,"svcgrp_name",svcgrp->name); /* Internal option */
		STK_ASSERT(STKA_SVCGRP,rc==STK_SUCCESS,"append service group name to options");

		/* Process service group options */
		{
			void *val;

			val = stk_find_option(options, "service_added_cb",NULL);
			if(val)
				svcgrp->svc_added_cb = (stk_service_added_cb) val;
			val = stk_find_option(options, "service_removed_cb",NULL);
			if(val)
				svcgrp->svc_removed_cb = (stk_service_removed_cb) val;
			val = stk_find_option(options, "service_smartbeat_cb",NULL);
			if(val)
				svcgrp->svc_smb_cb = (stk_service_smartbeat_cb) val;

			val = stk_find_option(options, "listening_data_flow",NULL);
			if(val) {
				rc = stk_sga_register_group_name(svcgrp,(stk_data_flow_t *)val);
				STK_CHECK(STKA_SVCGRP,rc==STK_SUCCESS,"register group name %s on df %p",svcgrp->name,val);
			}
		}


		svcgrp->activity_timer = stk_schedule_timer(stk_svc_group_timers,stk_svc_group_activity_cb,0,svcgrp,500);
		STK_ASSERT(STKA_SVCGRP,svcgrp->activity_timer!=NULL,"start timer for service group %p activity",svcgrp);
		if(svcgrp->activity_timer == NULL) {
			FreeList(svcgrp->svc_list);
			STK_FREE_STCT(STK_STCT_SERVICE_GROUP,svcgrp);
			svcgrp = NULL;
		}
	}
	return svcgrp;
}

stk_ret stk_destroy_service_group(stk_service_group_t *svc_group)
{
	STK_ASSERT(STKA_SVCGRP,svc_group->stct_type==STK_STCT_SERVICE_GROUP,"destroy a service group (%p), the pointer was to a structure of type %d",svc_group,svc_group->stct_type);

	if(svc_group->activity_timer) {
		stk_ret rc = stk_cancel_timer(stk_svc_group_timers,svc_group->activity_timer);
		STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"cancel activity timer for service group %p",svc_group);
	}

	if(svc_group->options) {
		stk_ret rc = stk_free_options(svc_group->options);
		STK_ASSERT(STKA_SVCGRP,rc==STK_SUCCESS,"free options array while destroying a service group");
	}
	if(svc_group->svc_list) {
		STK_ASSERT(STKA_SVCGRP,IsPListEmpty(svc_group->svc_list),"Destroyed a service group (%p) but the service list wasn't empty",svc_group);
		FreeList(svc_group->svc_list);
		svc_group->svc_list = (List *) 0xdeafbeef;
	}
	if(svc_group->name) free(svc_group->name);
	STK_FREE_STCT(STK_STCT_SERVICE_GROUP,svc_group);

	if(STK_ATOMIC_DECR(&timer_refcount) == 1) {
		stk_ret rc = stk_free_timer_set(stk_svc_group_timers,STK_TRUE);
		STK_ASSERT(STKA_NET,rc == STK_SUCCESS,"free timer set for service groups");
	}

	return STK_SUCCESS;
}

void stk_svc_group_activity_cb(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type)
{
	stk_service_group_t *svc_group = (stk_service_group_t *) userdata;
	stk_smartbeat_t curr_time;
	Node * nxt;
	stk_ret rc;

	STK_DEBUG(STKA_SVCGRP,"stk_svc_group_activity_cb called for service group %p",svc_group);
	stk_smartbeat_update_current_time(&curr_time);

	/* Iterate over all the services and check the last activity time */
	for(Node *n = FirstNode(svc_group->svc_list); !AtListEnd(n); n = nxt) {
		stk_service_state_in_group_t *svcingrp = NodeData(n);
		stk_smartbeat_t svc_time;

		nxt = NxtNode(n);

		if(svcingrp == NULL) continue;

		stk_get_service_smartbeat(svcingrp->service,&svc_time);

		if(stk_has_smartbeat_timed_out(&svc_time,&curr_time,stk_get_service_activity_tmo(svcingrp->service))) {
			stk_service_t *svc = svcingrp->service;
			stk_service_state last_state = STK_SERVICE_STATE_TIMED_OUT;

			/* Service has timed out */
			STK_DEBUG(STKA_SVCGRP,"stk_svc_group_activity_cb service %p has timed out %ld.%ld %d",svcingrp->service,svc_time.sec,svc_time.usec,stk_get_service_activity_tmo(svcingrp->service));
			stk_remove_service_node_from_group(svc_group,svc,n,svcingrp);

			rc = stk_destroy_service(svc,&last_state);
			STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"destroy service %p",svc);
		}
	}

	if(cb_type != STK_TIMER_CANCELLED) {
		rc = stk_reschedule_timer(timer_set,timer);
		STK_ASSERT(STKA_SMB,rc==STK_SUCCESS,"reschedule service group smartbeat timer %p",timer);
	}
}

stk_ret stk_set_service_group_state(stk_service_group_t *svc_group,stk_service_t *svc,stk_service_group_state state)
{
	STK_ASSERT(STKA_SVCGRP,svc_group!=NULL,"service group null or invalid :%p",svc_group);
	STK_ASSERT(STKA_SVCGRP,svc_group->stct_type==STK_STCT_SERVICE_GROUP,"change the state on a service group (%p), the pointer was to a structure of type %d",svc_group,svc_group->stct_type);

	svc_group->state = state;

	return STK_SUCCESS;
}

stk_service_group_state stk_get_service_group_state(stk_service_group_t *svc_group,stk_service_t *svc)
{
	STK_ASSERT(STKA_SVCGRP,svc_group!=NULL,"service group null or invalid :%p",svc_group);
	STK_ASSERT(STKA_SVCGRP,svc_group->stct_type==STK_STCT_SERVICE_GROUP,"get the state on a service group (%p), the pointer was to a structure of type %d",svc_group,svc_group->stct_type);
	return svc_group->state;
}

stk_ret stk_add_service_to_group(stk_service_group_t *svc_group,stk_service_t *svc,struct sockaddr_in ip,stk_service_in_group_state state)
{
	stk_service_state_in_group_t *svcingrp;
#ifdef STK_LITE
	if(NodeCount(svc_group->svc_list) >= STK_SERVICE_LIMIT)
		return !STK_SUCCESS;
#endif
	STK_CALLOC_STCT(STK_STCT_SERVICE_IN_GROUP,stk_service_state_in_group_t,svcingrp);
	if(svcingrp) {
		Node *n;
		svcingrp->service = svc;
		svcingrp->state = state;
		svcingrp->ip = ip;

		n = NewNode();
		if(!n) {
			STK_FREE_STCT(STK_STCT_SERVICE_IN_GROUP,svcingrp);
			return !STK_SUCCESS;
		}

		SetData(n,svcingrp);

		AddHead(svc_group->svc_list,n);

		if(svc_group->svc_added_cb) {
			STK_DEBUG(STKA_SVCGRP,"stk_add_service_to_group calling svc_added_cb service group %p service %lx %p state %d",
				svc_group,stk_get_service_id(svc),svc,state);
			svc_group->svc_added_cb(svc_group,svc,state);
		}
	}

	return STK_SUCCESS;
}

void stk_remove_service_node_from_group(stk_service_group_t *svc_group,stk_service_t *svc,Node *n,stk_service_state_in_group_t *st)
{
	Remove(n);
	if(svc_group->svc_removed_cb) {
		STK_DEBUG(STKA_SVCGRP,"stk_remove_service_node_from_group calling svc_remove_cb service group %p service %p state %d",svc_group,svc,st->state);
		svc_group->svc_removed_cb(svc_group,svc,st->state);
	}
	STK_FREE_STCT(STK_STCT_SERVICE_IN_GROUP,st);
	SetData(n,NULL);
	FreeNode(n);
}

/*
 * Removes a service from a group.
 * Returns success if the service was removed, or not success if nothing was removed
 */
stk_ret stk_remove_service_from_group(stk_service_group_t *svc_group,stk_service_t *svc)
{
	Node *nxt;

	STK_ASSERT(STKA_SVCGRP,svc_group!=NULL,"service group null or invalid :%p",svc_group);
	STK_ASSERT(STKA_SVCGRP,svc_group->stct_type==STK_STCT_SERVICE_GROUP,"service group %p passed in to stk_remove_service_from_group is structure type %d",svc_group,svc_group->stct_type);
	STK_ASSERT(STKA_SVCGRP,svc_group->svc_list!=NULL,"service group %p has service list",svc_group);
	for(Node *n = FirstNode(svc_group->svc_list); !AtListEnd(n); n = nxt) {
		stk_service_state_in_group_t *st = ((stk_service_state_in_group_t *) NodeData(n));
		nxt = NxtNode(n);
		if(st->service == svc) {
			stk_remove_service_node_from_group(svc_group,svc,n,st);
			return STK_SUCCESS;
		}
	}
	return !STK_SUCCESS;
}

stk_ret stk_set_service_state_in_group(stk_service_group_t *svc_group,stk_service_t *svc,stk_service_in_group_state state)
{
	STK_ASSERT(STKA_SVCGRP,svc_group!=NULL,"service group null or invalid :%p",svc_group);
	STK_ASSERT(STKA_SVCGRP,svc_group->stct_type==STK_STCT_SERVICE_GROUP,"service group %p passed in to stk_set_service_state_in_group is structure type %d",svc_group,svc_group->stct_type);
	STK_ASSERT(STKA_SVCGRP,svc_group->svc_list!=NULL,"service group %p has no service list",svc_group);
	for(Node *n = FirstNode(svc_group->svc_list); !AtListEnd(n); n = NxtNode(n)) {
		stk_service_state_in_group_t *st = ((stk_service_state_in_group_t *) NodeData(n));
		if(st->service == svc) {
			st->state = state;
			return STK_SUCCESS;
		}
	}
	return !STK_SUCCESS;
}

stk_service_in_group_state stk_get_service_state_in_group(stk_service_group_t *svc_group,stk_service_t *svc)
{
	STK_ASSERT(STKA_SVCGRP,svc_group!=NULL,"service group null or invalid :%p",svc_group);
	STK_ASSERT(STKA_SVCGRP,svc_group->stct_type==STK_STCT_SERVICE_GROUP,"service group %p passed in to stk_service_in_group_state_t is structure type %d",svc_group,svc_group->stct_type);
	STK_ASSERT(STKA_SVCGRP,svc_group->svc_list!=NULL,"service group %p has no service list",svc_group);
	for(Node *n = FirstNode(svc_group->svc_list); !AtListEnd(n); n = NxtNode(n)) {
		stk_service_state_in_group_t *st = ((stk_service_state_in_group_t *) NodeData(n));
		if(st->service == svc)
			return st->state;
	}
	return STK_SERVICE_IN_GROUP_ERROR;
}

stk_service_t *stk_find_service_in_group_by_name(stk_service_group_t *svc_group,char *name,struct sockaddr_in ip)
{
	STK_ASSERT(STKA_SVCGRP,svc_group!=NULL,"service group null or invalid :%p",svc_group);
	STK_ASSERT(STKA_SVCGRP,svc_group->stct_type==STK_STCT_SERVICE_GROUP,"service group %p passed in to stk_service_in_group_state_t is structure type %d",svc_group,svc_group->stct_type);
	STK_ASSERT(STKA_SVCGRP,svc_group->svc_list!=NULL,"service group %p has no service list",svc_group);
	for(Node *n = FirstNode(svc_group->svc_list); !AtListEnd(n); n = NxtNode(n)) {
		stk_service_state_in_group_t *st = ((stk_service_state_in_group_t *) NodeData(n));

		if(st->ip.sin_addr.s_addr != ip.sin_addr.s_addr || st->ip.sin_port != ip.sin_port) continue;

		if(!strcasecmp(stk_get_service_name(st->service),name))
			return st->service;
	}
	return NULL;
}

stk_service_t *stk_find_service_in_group_by_id(stk_service_group_t *svc_group,stk_service_id id,struct sockaddr_in ip)
{
	STK_ASSERT(STKA_SVCGRP,svc_group!=NULL,"service group null or invalid :%p",svc_group);
	STK_ASSERT(STKA_SVCGRP,svc_group->stct_type==STK_STCT_SERVICE_GROUP,"service group %p passed in to stk_service_in_group_state_t is structure type %d",svc_group,svc_group->stct_type);
	STK_ASSERT(STKA_SVCGRP,svc_group->svc_list!=NULL,"service group %p has no service list",svc_group);
	for(Node *n = FirstNode(svc_group->svc_list); !AtListEnd(n); n = NxtNode(n)) {
		stk_service_state_in_group_t *st = ((stk_service_state_in_group_t *) NodeData(n));

		if(st->ip.sin_addr.s_addr != ip.sin_addr.s_addr || st->ip.sin_port != ip.sin_port) continue;

		if(stk_get_service_id(st->service) == id)
			return st->service;
	}
	return NULL;
}

stk_ret stk_iterate_service_group(stk_service_group_t *svc_group,stk_service_in_group_cb cb,void *clientd)
{
	stk_ret rc = STK_SUCCESS;

	STK_ASSERT(STKA_SVCGRP,svc_group!=NULL,"service group null or invalid :%p",svc_group);
	STK_ASSERT(STKA_SVCGRP,svc_group->stct_type==STK_STCT_SERVICE_GROUP,"service group %p passed in to stk_service_in_group_state_t is structure type %d",svc_group,svc_group->stct_type);
	STK_ASSERT(STKA_SVCGRP,svc_group->svc_list!=NULL,"service group %p has no service list",svc_group);
	for(Node *n = FirstNode(svc_group->svc_list); !AtListEnd(n); n = NxtNode(n)) {
		stk_service_state_in_group_t *st = ((stk_service_state_in_group_t *) NodeData(n));

		rc = cb(svc_group,st->service,clientd);
		if(rc != STK_SUCCESS) return rc;
	}
	return STK_SUCCESS;
}


stk_env_t *stk_get_service_group_env(stk_service_group_t *svc_group)
{
	STK_ASSERT(STKA_SVCGRP,svc_group!=NULL,"service group null or invalid :%p",svc_group);
	STK_ASSERT(STKA_SVCGRP,svc_group->stct_type==STK_STCT_SERVICE_GROUP,"service group %p passed in to stk_get_service_group_env is structure type %d",svc_group,svc_group->stct_type);
	return svc_group->env;
}

char *stk_get_service_group_name(stk_service_group_t *svc_group)
{
	STK_ASSERT(STKA_SVCGRP,svc_group!=NULL,"service group null or invalid :%p",svc_group);
	STK_ASSERT(STKA_SVCGRP,svc_group->stct_type==STK_STCT_SERVICE_GROUP,"service group %p passed in to stk_get_service_group_name is structure type %d",svc_group,svc_group->stct_type);
	return svc_group->name;
}

stk_options_t *stk_get_service_group_options(stk_service_group_t *svc_group)
{
	STK_ASSERT(STKA_SVCGRP,svc_group!=NULL,"service group null or invalid :%p",svc_group);
	STK_ASSERT(STKA_SVCGRP,svc_group->stct_type==STK_STCT_SERVICE_GROUP,"service group %p passed in to stk_get_service_group_options is structure type %d",svc_group,svc_group->stct_type);

	return svc_group->options;
}

stk_ret stk_service_group_handle_smartbeat(stk_service_group_t *svc_group,stk_service_id svc_id,stk_smartbeat_t *smartbeat,struct sockaddr_in reporting_ip)
{
	stk_service_t *svc = stk_find_service_in_group_by_id(svc_group,svc_id,reporting_ip);
	STK_DEBUG(STKA_SVCGRP,"stk_service_group_handle_smartbeat called for service %lx %p svc_group %p",svc_id,svc,svc_group);
	if(svc) {
		stk_service_update_smartbeat(svc,smartbeat);
		if(svc_group->svc_smb_cb)
			svc_group->svc_smb_cb(svc_group,svc,smartbeat);
	}

	return STK_SUCCESS;
}


stk_service_group_id stk_get_service_group_id(stk_service_group_t *svcgrp) { return svcgrp->id; }

