#include "stk_smartbeat_api.h"
#include "stk_data_flow_api.h"
#include "stk_service_api.h"
#include "stk_env_api.h"
#include "stk_common.h"
#include "stk_internal.h"
#include "stk_timer_api.h"
#include "stk_sequence_api.h"
#include "stk_name_service_api.h"
#include "stk_sg_automation_api.h"
#include "PLists.h"
#include <sys/time.h>
#include <string.h>

#define STK_SMB_TIMER_ID 0x20000000
#define STK_SMB_TIMER_IVL 500

#define STK_STCT_SVC_SMARTBEAT 0x50000000

struct stk_smartbeat_ctrl_stct {
	stk_stct_type stct_type;
	stk_env_t *env;
	List *service_list;
	List *name_service_list;
	stk_timer_set_t *timer_set;
	stk_timer_t *scheduled_timer;
	int timer_ivl;
};

stk_smartbeat_ctrl_t *stk_create_smartbeat_ctrl(stk_env_t *env)
{
	stk_smartbeat_ctrl_t * smb;
	STK_CALLOC_STCT(STK_STCT_SMARTBEAT,stk_smartbeat_ctrl_t,smb);
	if(smb) {
		smb->env = env;
		smb->service_list = NewPList();
		STK_ASSERT(STKA_SMB,smb->service_list!=NULL,"alloc service list");
		smb->name_service_list = NewPList();
		STK_ASSERT(STKA_SMB,smb->name_service_list!=NULL,"alloc name service list");
		smb->timer_set = stk_new_timer_set(env,0,0,STK_TRUE);
		smb->timer_ivl = STK_SMB_TIMER_IVL;
	}
	return smb;
}

stk_ret stk_destroy_smartbeat_ctrl(stk_smartbeat_ctrl_t *smb)
{
	STK_ASSERT(STKA_SMB,smb->stct_type==STK_STCT_SMARTBEAT,"destroy a smartbeat controller, the pointer was to a structure of type %d",smb->stct_type);

	if(smb->timer_set) {
		stk_ret rc = stk_free_timer_set(smb->timer_set,1);
		STK_ASSERT(STKA_SMB,rc==STK_SUCCESS,"free timer set in stk_destroy_smartbeat_ctrl");
	}

	if(smb->service_list) FreeList(smb->service_list);
	if(smb->name_service_list) FreeList(smb->name_service_list);

	STK_FREE_STCT(STK_STCT_SMARTBEAT,smb);
	return STK_SUCCESS;
}

int stk_min_smartbeat_interval(stk_smartbeat_ctrl_t *smb)
{
	STK_ASSERT(STKA_SMB,smb->stct_type==STK_STCT_SMARTBEAT,"get the minimum timer interval from a smartbeat controller, the pointer was to a structure of type %d",smb->stct_type);
	return smb->timer_ivl;
}

stk_ret stk_smartbeat_update_current_time(stk_smartbeat_t *sb)
{
	struct timeval tv;
	int rc = gettimeofday(&tv,NULL);
	sb->sec = (stk_uint64) tv.tv_sec;
	sb->usec = (stk_uint64) tv.tv_usec;
	if(rc == -1) {
		STK_LOG(STK_LOG_ERROR,"gettimeofday failed!");
		return !STK_SUCCESS;
	}
	else return STK_SUCCESS;
}

void stk_send_smartbeat_to_services(stk_smartbeat_ctrl_t *smb, stk_timer_set_t *timer_set)
{
	stk_sequence_t *seq;
	stk_data_flow_t **dest_flows = calloc(sizeof(stk_data_flow_t *),1); int array_sz = 1;
	stk_smartbeat_t curr_time;
	stk_ret rc;
	int ipadded = STK_FALSE;
	struct sockaddr_in *ipref = NULL;

	STK_ASSERT(STKA_SMB,dest_flows!=NULL,"allocate memory for destination flows");

	/* Create smartbeat sequence and create list of data flows to send to */
	seq = stk_create_sequence(stk_env_from_timer_set(timer_set),"Smartbeat", STK_SMARTBEAT_SEQ, STK_SEQUENCE_TYPE_MGMT,STK_SERVICE_TYPE_MGMT, NULL);
	STK_ASSERT(STKA_SMB,seq != NULL,"allocate smartbeat sequence");

	rc = stk_smartbeat_update_current_time(&curr_time);
	STK_ASSERT(STKA_SMB,rc==STK_SUCCESS,"get current time in stk_smb_timer_cb");

	/* Send to service notification data flows this smart beat */
	for(Node *n = FirstNode(smb->service_list); !AtListEnd(n); n = NxtNode(n)) {

		/* Update the destination flows for this service */
		stk_service_t *svc = (stk_service_t *) NodeData(n);
		stk_data_flow_t **svc_flows = stk_svc_get_smartbeat_flows(svc);

		if(!ipadded) {
			stk_data_flow_t *df = stk_get_notification_df(svc);

			if(!df) df = stk_get_monitoring_df(svc);

			if(df) {
				ipref = stk_sga_add_service_reporting_ip_ref(seq,df);
				STK_ASSERT(STKA_SMB,ipref!=NULL,"add reporting IP in stk_smb_timer_cb");
				ipadded = STK_TRUE;
			}
		}

		if(svc_flows) {
			/* Add the flows used by this service to our destination list (ignoring dups) */
			int svc_flow_idx = 0;
			while(svc_flows[svc_flow_idx]) {
				int dest_flow_idx = 0;

				while(dest_flows[dest_flow_idx] != svc_flows[svc_flow_idx] && dest_flows[dest_flow_idx] != NULL) dest_flow_idx++;

				if(dest_flows[dest_flow_idx] == NULL) {
					/* Add to destination, not the most efficient way with multiple reallocs... */
					dest_flows = realloc(dest_flows,sizeof(stk_data_flow_t *) * (array_sz + 1));
					dest_flows[array_sz - 1] = svc_flows[svc_flow_idx];
					dest_flows[array_sz++] = NULL;
				}

				svc_flow_idx++;
			}

			free(svc_flows);
		}

		/* Add this service to the heartbeat sequence */
		{
		stk_smartbeat_svc_wire_t svcsmb;
		stk_smartbeat_t svctime;
		stk_ret rc;

		svcsmb.service = stk_get_service_id(svc);
		/* Get the current smartbeat from this service so we have the checkpoint etc */
		stk_get_service_smartbeat(svc,&svctime);

		/* Update and get current time */
		svctime.sec = curr_time.sec;
		svctime.usec = curr_time.usec;
		memcpy(&svcsmb.smartbeat,&svctime,sizeof(svcsmb.smartbeat));

		rc = stk_copy_to_sequence(seq,&svcsmb,sizeof(stk_smartbeat_svc_wire_t),STK_STCT_SVC_SMARTBEAT_WIRE);
		STK_ASSERT(STKA_SMB,rc==STK_SUCCESS,"Copy to sequence %p failed in stk_smb_timer_cb",seq);
		}
	}
	STK_ASSERT(STKA_SMB,ipref!=NULL,"verify reporting IP in stk_smb_timer_cb");

	/* send to data flows on list */
	for(int idx = 0; idx < array_sz && dest_flows[idx]; idx++) {
		stk_ret getiprc = stk_data_flow_id_ip(dest_flows[idx],(struct sockaddr *) ipref,sizeof(*ipref));
		STK_CHECK(STKA_SMB,getiprc==STK_SUCCESS,"get IP for df %p - dropping smartbeat",dest_flows[idx]);

		ipref->sin_addr.s_addr = htonl(ipref->sin_addr.s_addr);
		ipref->sin_port = htons(ipref->sin_port);
		STK_ASSERT(STKA_NET,1,"update IP address %x:%d to sequence %p", ntohl(ipref->sin_addr.s_addr), ntohs(ipref->sin_port), seq);

		if(getiprc==STK_SUCCESS) {
			STK_DEBUG(STKA_SMB,"SENDING SMARTBEAT services df %p REPLACE IP %x:%d",dest_flows[idx],ipref->sin_addr.s_addr,ipref->sin_port);
			stk_ret smartbeat_sent = stk_data_flow_send(dest_flows[idx],seq,0);
			STK_CHECK(STKA_SMB,smartbeat_sent==STK_SUCCESS,"send smartbeat to data flow %p (idx %d)",dest_flows[idx],idx);
		}
	}
	free(dest_flows);
	if(ipref!=NULL) free(ipref);

	/* Done sending, free resources */
	{
	/* free the data in the sequence */
	rc = stk_destroy_sequence(seq);
	STK_ASSERT(STKA_SMB,rc==STK_SUCCESS,"destroy smartbeat sequence %p",seq);
	}
}

void stk_send_smartbeat_to_name_services(stk_smartbeat_ctrl_t *smb, stk_timer_set_t *timer_set)
{
	stk_sequence_t *seq;
	stk_smartbeat_t curr_time;
	stk_ret rc;
	int ipadded = STK_FALSE;
	struct sockaddr_in *ipref = NULL;

	/* Create smartbeat sequence and create list of data flows to send to */
	seq = stk_create_sequence(stk_env_from_timer_set(timer_set),"Smartbeat", STK_SMARTBEAT_SEQ, STK_SEQUENCE_TYPE_MGMT,STK_SERVICE_TYPE_MGMT, NULL);
	STK_ASSERT(STKA_SMB,seq != NULL,"allocate smartbeat sequence");

	rc = stk_smartbeat_update_current_time(&curr_time);
	STK_ASSERT(STKA_SMB,rc==STK_SUCCESS,"get current time in stk_send_smartbeat_to_name_services");

	/* Send to service notification data flows this smart beat */
	for(Node *n = FirstNode(smb->name_service_list); !AtListEnd(n); n = NxtNode(n)) {
		/* Update the destination flows for this service */
		stk_name_service_t *svc = (stk_name_service_t *) NodeData(n);
		stk_data_flow_t **svc_flows = stk_ns_get_smartbeat_flows(svc);

		if(!svc_flows) continue;

		STK_DEBUG(STKA_SMB,"stk_send_smartbeat_to_name_services name service %p",svc);

		/* Add this service to the heartbeat sequence */
		{
		stk_smartbeat_svc_wire_t svcsmb;
		stk_smartbeat_t svctime;
		stk_ret rc;

		svcsmb.service = (stk_service_id) NULL;

		/* Update and get current time */
		svctime.sec = curr_time.sec;
		svctime.usec = curr_time.usec;
		svctime.checkpoint = 0;
		memcpy(&svcsmb.smartbeat,&svctime,sizeof(svcsmb.smartbeat));

		rc = stk_copy_to_sequence(seq,&svcsmb,sizeof(stk_smartbeat_svc_wire_t),STK_STCT_SVC_SMARTBEAT_WIRE);
		STK_ASSERT(STKA_SMB,rc==STK_SUCCESS,"Copy to sequence %p failed in stk_send_smartbeat_to_name_services",seq);
		}

		/* send to data flows on list */
		{
		int idx = 0;
		while(svc_flows[idx])
		{
			stk_ret getiprc;

			if(!ipadded) {
				stk_data_flow_t *df = svc_flows[idx];

				ipref = stk_sga_add_service_reporting_ip_ref(seq,df);
				STK_ASSERT(STKA_SMB,ipref!=NULL,"add reporting IP in stk_send_smartbeat_to_name_services");
				ipadded = STK_TRUE;
			}

			getiprc = stk_data_flow_id_ip(svc_flows[idx],(struct sockaddr *) ipref,sizeof(*ipref));
			STK_CHECK(STKA_SMB,getiprc==STK_SUCCESS,"get IP for df %p - dropping smartbeat",svc_flows[idx]);

			ipref->sin_addr.s_addr = htonl(ipref->sin_addr.s_addr);
			ipref->sin_port = htons(ipref->sin_port);
			STK_ASSERT(STKA_NET,1,"update IP address %x:%d to sequence %p", ntohl(ipref->sin_addr.s_addr), ntohs(ipref->sin_port), seq);

			if(getiprc==STK_SUCCESS) {
				STK_DEBUG(STKA_SMB,"SENDING SMARTBEAT name service %p df %p REPLACE IP %x:%d",svc,svc_flows[idx],ipref->sin_addr.s_addr,ipref->sin_port);
				stk_ret smartbeat_sent = stk_data_flow_send(svc_flows[idx],seq,0);
				STK_DEBUG(STKA_SMB,"send smartbeat to data flow %p (idx %d) rc %d",svc_flows[idx],idx,smartbeat_sent);
			}
			idx++;
		}
		}
		free(svc_flows);
	}
	STK_ASSERT(STKA_SMB,ipref!=NULL,"verify reporting IP in stk_send_smartbeat_to_name_services");
	if(ipref!=NULL) free(ipref);

	/* Done sending, free resources */
	{
	/* free the data in the sequence */
	rc = stk_destroy_sequence(seq);
	STK_ASSERT(STKA_SMB,rc==STK_SUCCESS,"destroy smartbeat sequence %p",seq);
	}
}

void stk_smb_timer_cb(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type)
{
	stk_smartbeat_ctrl_t *smb = (stk_smartbeat_ctrl_t *) userdata;

	STK_DEBUG(STKA_SMB,"**** stk_smb_timer_cb type %d",cb_type);

	if(cb_type == STK_TIMER_EXPIRED) {
		stk_ret rc;

		if(!IsPListEmpty(smb->service_list)) {
			STK_DEBUG(STKA_SMB,"stk_smb_timer_cb sending to services");
			stk_send_smartbeat_to_services(smb,timer_set);
		}

		if(!IsPListEmpty(smb->name_service_list)) {
			STK_DEBUG(STKA_SMB,"stk_smb_timer_cb sending to name services");
			stk_send_smartbeat_to_name_services(smb,timer_set);
		}

		rc = stk_reschedule_timer(timer_set,timer);
		STK_ASSERT(STKA_SMB,rc==STK_SUCCESS,"reschedule smartbeat timer %p",timer);
	}
	STK_DEBUG(STKA_SMB,"**** stk_smb_timer_cb ending");
}

stk_ret stk_smartbeat_add_service(stk_smartbeat_ctrl_t *smb, stk_service_t *svc)
{
	Node *n = NewNode();

	STK_ASSERT(STKA_SMB,smb->stct_type==STK_STCT_SMARTBEAT,"add service %p to a smartbeat controller, the pointer was to a structure of type %d",svc,smb->stct_type);
	STK_CHECK(STKA_SMB,n!=NULL,"allocate a node for the service list");

	if(n) {
		SetData(n,svc);
		AddTail(smb->service_list,n);

		stk_send_smartbeat_to_services(smb,smb->timer_set);

		if(smb->scheduled_timer == NULL)
			smb->scheduled_timer = stk_schedule_timer(smb->timer_set,stk_smb_timer_cb,STK_SMB_TIMER_ID,smb,STK_SMB_TIMER_IVL);

		return STK_SUCCESS;
	}

	return STK_MEMERR;
}

stk_ret stk_smartbeat_remove_service(stk_smartbeat_ctrl_t *smb, stk_service_t *svc)
{
	STK_ASSERT(STKA_SMB,smb->stct_type==STK_STCT_SMARTBEAT,"remove service %p from a smartbeat controller, the pointer was to a structure of type %d",svc,smb->stct_type);

	if(IsPListEmpty(smb->service_list)) return STK_SUCCESS;

	for(Node *n = FirstNode(smb->service_list); !AtListEnd(n); n = NxtNode(n)) {
		if(NodeData(n) == svc) {
			Remove(n);
			SetData(n,NULL);
			FreeNode(n);
			return STK_SUCCESS;
		}
	}
	return !STK_SUCCESS;
}

stk_ret stk_smartbeat_add_name_service(stk_smartbeat_ctrl_t *smb, stk_name_service_t *svc)
{
	Node *n = NewNode();

	STK_ASSERT(STKA_SMB,smb->stct_type==STK_STCT_SMARTBEAT,"add name service %p to a smartbeat controller, the pointer was to a structure of type %d",svc,smb->stct_type);
	STK_CHECK(STKA_SMB,n!=NULL,"allocate a node for the name service list");

	if(n) {
		SetData(n,svc);
		AddTail(smb->name_service_list,n);

		stk_send_smartbeat_to_name_services(smb,smb->timer_set);

		STK_CHECK(STKA_SMB,smb->scheduled_timer == NULL,"scheduled timer is null - interval %d",STK_SMB_TIMER_IVL);
		if(smb->scheduled_timer == NULL)
			smb->scheduled_timer = stk_schedule_timer(smb->timer_set,stk_smb_timer_cb,STK_SMB_TIMER_ID,smb,STK_SMB_TIMER_IVL);

		return STK_SUCCESS;
	}

	return STK_MEMERR;
}

stk_ret stk_smartbeat_remove_name_service(stk_smartbeat_ctrl_t *smb, stk_name_service_t *named)
{
	STK_ASSERT(STKA_SMB,smb->stct_type==STK_STCT_SMARTBEAT,"remove name service %p from a smartbeat controller, the pointer was to a structure of type %d",named,smb->stct_type);

	for(Node *n = FirstNode(smb->name_service_list); !AtListEnd(n); n = NxtNode(n)) {
		if(NodeData(n) == named) {
			Remove(n);
			SetData(n,NULL);
			FreeNode(n);
			STK_DEBUG(STKA_SMB,"removed name service %p from smartbeat controller %p",named,smb);
			return STK_SUCCESS;
		}
	}
	return !STK_SUCCESS;
}


#define smb_timercmp(a, b, CMP)  (((a)->sec == (b)->sec) ?  ((a)->usec CMP (b)->usec) : ((a)->sec CMP (b)->sec))

stk_bool stk_has_smartbeat_timed_out(stk_smartbeat_t *sb,stk_smartbeat_t *curr_time,stk_uint32 ivl)
{
	stk_smartbeat_t tv;
	int res;

	tv.sec = sb->sec;
	tv.usec = sb->usec;

	tv.usec += (ivl*1000);
	tv.sec += tv.usec / 1000000;
	tv.usec = tv.usec % 1000000;

	res = smb_timercmp(&tv,curr_time,<);
	STK_DEBUG(STKA_HTTPD,"smartbeat activity %ld.%ld curr time %ld.%ld timed out %d",tv.sec % 86400 /* day */,tv.usec,curr_time->sec % 86400,curr_time->usec,res);
	if(res)
		return STK_TRUE;
	else
		return STK_FALSE;
}

