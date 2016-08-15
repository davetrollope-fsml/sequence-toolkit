#include "stk_service_api.h"
#include "stk_service.h"
#include "stk_common.h"
#include "stk_internal.h"
#include "stk_data_flow_api.h"
#include "stk_options_api.h"
#include "stk_sequence_api.h"
#include "stk_sg_automation_api.h"
#include "stk_smartbeat_api.h"
#include "stk_env_api.h"
#include "stk_tcp_client_api.h"
#include "stk_udp_client_api.h"
#include <string.h>

#ifdef STK_LITE
#define STK_SERVICE_LIMIT 25
int service_count;
#endif

typedef void * stk_service_metadata;

/* A structure to map a state to a string */
typedef struct { stk_service_state state; char *name; } stk_service_state_name;

struct stk_service_stct
{
	stk_stct_type stct_type;
	stk_env_t *env;
	stk_service_id id;
	char *name;
	stk_service_type type;
	stk_service_metadata meta_data;
	stk_service_state state;
	stk_data_flow_t *notification_df;
	stk_data_flow_t *monitoring_df;
	stk_smartbeat_t smartbeat;
	stk_uint32 activity_tmo;
	stk_service_state_change_cb state_change_cb;
	stk_service_state_name *state_names;
	int state_names_sz;
	stk_options_t *options;
};

stk_service_state_name stk_service_state_default_names[] = {
	{ STK_SERVICE_STATE_STARTING, "starting"},
	{ STK_SERVICE_STATE_RUNNING, "running"},
	{ STK_SERVICE_STATE_STOPPING, "stopping"},
	{ STK_SERVICE_STATE_STOPPED, "stopped"},
	{ STK_SERVICE_STATE_TIMED_OUT, "timed out"},
	{ STK_SERVICE_STATE_INVALID, NULL}
};

void stk_update_service_state(stk_service_t *svc,stk_service_state state);

stk_service_t *stk_create_service(stk_env_t *env,char *name, stk_service_id id, stk_service_type type, stk_options_t *options)
{
	stk_service_t * svc;
	char *optstr;
	int activity_tmo = 1200;
	stk_create_data_flow_t monitoring_create_df = stk_tcp_client_create_data_flow;
	stk_create_data_flow_t notification_create_df = stk_tcp_client_create_data_flow;

#ifdef STK_LITE
	if(service_count >= STK_SERVICE_LIMIT) return NULL;
	service_count++;
#endif

	/* Service activity is determined by the configuration of the activity timeout (ms) */
	optstr = stk_find_option(options,"activity_timeout",NULL);
	if(optstr) {
		int min_activity_tmo = stk_min_smartbeat_interval(stk_env_get_smartbeat_ctrl(env));
		activity_tmo = atoi(optstr);
		if(activity_tmo < min_activity_tmo) {
			STK_LOG(STK_LOG_ERROR,"activity_timeout %d is less than the minimum %d in stk_create_service",activity_tmo,min_activity_tmo);
			return NULL;
		}
	}

	/* Determine if the monitoring data flow protocol is configured */
	optstr = stk_find_option(options,"monitoring_data_flow_protocol",NULL);
	if(optstr && !strcasecmp(optstr,"udp"))
		monitoring_create_df = stk_udp_client_create_data_flow;

	/* Determine if the notification data flow protocol is configured */
	optstr = stk_find_option(options,"notification_data_flow_protocol",NULL);
	if(optstr && !strcasecmp(optstr,"udp"))
		notification_create_df = stk_udp_client_create_data_flow;

	STK_CALLOC_STCT(STK_STCT_SERVICE,stk_service_t,svc);
	if(svc) {
		svc->env = env;
		svc->name = name ? strdup(name) : NULL;
		svc->id = id;
		svc->type = type;
		svc->state = STK_SERVICE_STATE_STARTING;
		svc->activity_tmo = activity_tmo;
		svc->state_names = stk_service_state_default_names;

		/* Store service state change callback */
		svc->state_change_cb = (stk_service_state_change_cb) stk_find_option(options, "state_change_cb",NULL);

		/* Copy options for python API to get state change cb */
		svc->options = stk_copy_extend_options(options, 0);

		/* Service monitoring is determined by the configuration of a monitoring data flow */
		svc->monitoring_df = stk_data_flow_process_extended_options(env,options,"monitoring_data_flow",monitoring_create_df);

		/* Service automation is determined by the configuration of a notification data flow */
		svc->notification_df = stk_data_flow_process_extended_options(env,options,"notification_data_flow",notification_create_df);

		STK_DEBUG(STKA_NET,"Monitoring %p notification %p",svc->monitoring_df,svc->notification_df);

		stk_smartbeat_update_current_time(&svc->smartbeat);

		if(svc->notification_df || svc->monitoring_df) {
			struct sockaddr_in *ipref = NULL;
			stk_sequence_t *seq;
			stk_ret rc;

			seq = stk_create_sequence(svc->env,"create_service_notification_sequence",STK_SERVICE_NOTIF_CREATE,STK_SEQUENCE_TYPE_MGMT,STK_SERVICE_TYPE_MGMT,NULL);
			if(seq == NULL) {
				STK_LOG(STK_LOG_ERROR,"allocate create_service_notification_sequence");
				STK_CHECK(STKA_SVC,stk_destroy_service(svc,NULL)==STK_SUCCESS,"destroy partially created service");
				return NULL;
			}

			ipref = stk_sga_add_service_reporting_ip_ref(seq,svc->notification_df ? svc->notification_df : svc->monitoring_df);
			STK_CHECK(STKA_SVC,ipref!=NULL,"add reporting IP to sequence : %p",ipref);
			if(ipref == NULL) {
				rc = stk_destroy_sequence(seq);
				STK_CHECK(STKA_SVC,rc==STK_SUCCESS,"destroy the notification sequence : %d",rc);
				return STK_SUCCESS;
			}

			rc = stk_sga_add_service_op_to_sequence(seq,svc,STK_SGA_CREATE_SVC);
			if(rc != STK_SUCCESS) {
				STK_LOG(STK_LOG_ERROR,"add service creation to sequence : %d",rc);
				STK_CHECK(STKA_SVC,stk_destroy_service(svc,NULL)==STK_SUCCESS,"destroy partially created service");
				if(ipref) free(ipref);
				return NULL;
			}

			if(svc->monitoring_df != svc->notification_df && svc->notification_df) { /* No need to send to the same data flow */
				stk_ret getiprc = stk_data_flow_id_ip_nw(svc->notification_df,(struct sockaddr_in *) ipref,sizeof(*ipref));
				STK_CHECK(STKA_SMB,getiprc==STK_SUCCESS,"Couldn't get IP for notification df %p",svc->notification_df);

				rc = stk_data_flow_send(svc->notification_df,seq,0);
				if(rc != STK_SUCCESS) {
					STK_LOG(STK_LOG_ERROR,"send service creation (%s) to notification data flow (%p) failed (rc %d)",name,svc->notification_df,rc);
					STK_CHECK(STKA_SVC,stk_destroy_service(svc,NULL)==STK_SUCCESS,"destroy partially created service");
					if(ipref) free(ipref);
					return NULL;
				}
			}

			if(svc->monitoring_df) {
				char *group_name = stk_find_option(options,"svcgrp_name",NULL);

				if(group_name) {
					rc = stk_copy_to_sequence(seq,group_name,strlen(group_name) + 1, STK_SERVICE_GROUP_NAME);
					STK_CHECK(STKA_SVC,rc == STK_SUCCESS,"add group name to server create being sent to monitoring");
				}

				stk_ret getiprc = stk_data_flow_id_ip_nw(svc->monitoring_df,(struct sockaddr_in *) ipref,sizeof(*ipref));
				STK_CHECK(STKA_SMB,getiprc==STK_SUCCESS,"Couldn't get IP for monitoring df %p",svc->monitoring_df);

				rc = stk_data_flow_send(svc->monitoring_df,seq,0);
				if(rc != STK_SUCCESS) {
					STK_LOG(STK_LOG_ERROR,"send service creation (%s) to monitoring data flow (%p) failed (rc %d)",name,svc->notification_df,rc);
					STK_CHECK(STKA_SVC,stk_destroy_service(svc,NULL)==STK_SUCCESS,"destroy partially created service");
					if(ipref) free(ipref);
					return NULL;
				}
			}

			rc = stk_smartbeat_add_service(stk_env_get_smartbeat_ctrl(svc->env), svc);
			STK_ASSERT(STKA_SVCGRP,rc==STK_SUCCESS,"add service to smartbeat rc %d svc %p",rc,svc);

			/* free the data in the sequence */
			rc = stk_destroy_sequence(seq);
			if(rc != STK_SUCCESS) {
				STK_LOG(STK_LOG_ERROR,"destroy the notification sequence : %d",rc);
				if(ipref) free(ipref);
				return NULL;
			}

			if(ipref) free(ipref);
		}
	}
	return svc;
}

stk_ret stk_destroy_service(stk_service_t *svc,stk_service_state *last_state)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"destroy a service, the pointer was to a structure of type %d",svc->stct_type);

#ifdef STK_LITE
	service_count--;
#endif

	stk_smartbeat_update_current_time(&svc->smartbeat);

	if(svc->notification_df || svc->monitoring_df) {
		struct sockaddr_in *ipref = NULL;
		stk_sequence_t *seq;
		stk_ret rc;

		seq = stk_create_sequence(svc->env,"destroy_service_notification_sequence",STK_SERVICE_NOTIF_DESTROY,STK_SEQUENCE_TYPE_MGMT,STK_SERVICE_TYPE_MGMT,NULL);
		if(seq == NULL) {
			STK_LOG(STK_LOG_ERROR,"allocate destroy_service_notification_sequence");
			return STK_MEMERR;
		}

		if(last_state)
			stk_update_service_state(svc,*last_state);
		else
			stk_update_service_state(svc,STK_SERVICE_STATE_STOPPED);

		rc = stk_sga_add_service_state_name_to_sequence(seq,svc,last_state ? *last_state : STK_SERVICE_STATE_STOPPED);
		STK_CHECK(STKA_SVC,rc==STK_SUCCESS,"add service state name for state %d to sequence : %d",last_state ? *last_state : STK_SERVICE_STATE_STOPPED,rc);
		if(rc != STK_SUCCESS) {
			rc = stk_destroy_sequence(seq);
			STK_CHECK(STKA_SVC,rc==STK_SUCCESS,"destroy the notification sequence : %d",rc);
			if(ipref) free(ipref);
			return STK_SUCCESS;
		}

		ipref = stk_sga_add_service_reporting_ip_ref(seq,svc->notification_df ? svc->notification_df : svc->monitoring_df);
		STK_CHECK(STKA_SVC,ipref!=NULL,"add reporting IP to sequence : %d",rc);
		if(ipref == NULL) {
			rc = stk_destroy_sequence(seq);
			STK_CHECK(STKA_SVC,rc==STK_SUCCESS,"destroy the notification sequence : %d",rc);
			return STK_SUCCESS;
		}

		rc = stk_sga_add_service_op_to_sequence(seq,svc,STK_SGA_DESTROY_SVC);
		if(rc != STK_SUCCESS) {
			STK_LOG(STK_LOG_ERROR,"add service destruction to sequence : %d",rc);
			STK_CHECK(STKA_SVC,stk_destroy_sequence(seq)==STK_SUCCESS,"destroy sequence %p while destroying service",seq);
			if(ipref) free(ipref);
			return rc;
		}

		if(svc->monitoring_df) {
			stk_ret getiprc = stk_data_flow_id_ip_nw(svc->monitoring_df,(struct sockaddr_in *) ipref,sizeof(*ipref));
			STK_CHECK(STKA_SMB,getiprc==STK_SUCCESS,"Couldn't get IP for monitoring df %p",svc->monitoring_df);

			rc = stk_data_flow_send(svc->monitoring_df,seq,0);
			if(rc != STK_SUCCESS)
				STK_LOG(STK_LOG_ERROR,"error sending service destruction (%s) to monitoring data flow (%p) rc (%d)",svc->name,svc->monitoring_df,rc);
		}
		if(svc->monitoring_df != svc->notification_df && svc->notification_df) { /* No need to send to the same data flow */
			stk_ret getiprc = stk_data_flow_id_ip_nw(svc->notification_df,(struct sockaddr_in *) ipref,sizeof(*ipref));
			STK_CHECK(STKA_SMB,getiprc==STK_SUCCESS,"Couldn't get IP for notification df %p",svc->notification_df);

			rc = stk_data_flow_send(svc->notification_df,seq,0);
			if(rc != STK_SUCCESS)
				STK_LOG(STK_LOG_ERROR,"error sending service destruction (%s) to notification data flow (%p) rc (%d)",svc->name,svc->notification_df,rc);
		}

		/* free the data in the sequence */
		rc = stk_destroy_sequence(seq);
		if(rc != STK_SUCCESS)
			STK_LOG(STK_LOG_ERROR,"destroy the notification sequence : %d",rc);

		rc = stk_smartbeat_remove_service(stk_env_get_smartbeat_ctrl(svc->env), svc);
		if(rc != STK_SUCCESS)
			STK_LOG(STK_LOG_ERROR,"remove service from smartbeat controller: %d",rc);

		if(ipref) free(ipref);
	}

	if(svc->options) {
		stk_ret rc = stk_free_options(svc->options);
		STK_ASSERT(STKA_SVCGRP,rc==STK_SUCCESS,"free options array while destroying a service");
	}

	if(svc->state_names != stk_service_state_default_names)
		STK_FREE(svc->state_names);

	if(svc->name) {
		free(svc->name);
		svc->name = NULL;
	}

	STK_FREE_STCT(STK_STCT_SERVICE,svc);
	return STK_SUCCESS;
}

/* internal function */
void stk_update_service_state(stk_service_t *svc,stk_service_state state)
{
	stk_service_state old_state = svc->state;
	svc->state = state;

	/* Notify registered apps of state change */
	if(svc->state_change_cb)
		svc->state_change_cb(svc,old_state,state);
}

/* APIs to manage service state */
stk_ret stk_set_service_state(stk_service_t *svc,stk_service_state state)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"set service state (%d) on a structure of type %d",state,svc->stct_type);
	stk_update_service_state(svc,state);

	stk_smartbeat_update_current_time(&svc->smartbeat);

	/* From this point on, return success in the case of sending to the notification or monitoring services. The state was changed successfully */
	if(svc->notification_df || svc->monitoring_df) {
		struct sockaddr_in *ipref = NULL;
		stk_sequence_t *seq;
		stk_ret rc;

		seq = stk_create_sequence(svc->env,"service_state_notification_sequence",STK_SERVICE_NOTIF_STATE,STK_SEQUENCE_TYPE_MGMT,STK_SERVICE_TYPE_MGMT,NULL);
		STK_CHECK(STKA_SVC,seq!=NULL,"allocate service_state_notification_sequence");
		if(seq == NULL) return STK_SUCCESS;

		rc = stk_sga_add_service_name_to_sequence(seq,svc);
		if(rc != STK_SUCCESS) {
			char * c = (char *) stk_get_service_name(svc);
			STK_CHECK(STKA_SVCAUT,rc==STK_SUCCESS,"copy service name '%s' to sequence %p",c ? c : "",seq);
			rc = stk_destroy_sequence(seq);
			STK_CHECK(STKA_SVC,rc==STK_SUCCESS,"destroy the notification sequence : %d",rc);
			return STK_SUCCESS;
		}

		ipref = stk_sga_add_service_reporting_ip_ref(seq,svc->notification_df ? svc->notification_df : svc->monitoring_df);
		STK_CHECK(STKA_SVC,ipref!=NULL,"add reporting IP to sequence : %d",rc);
		if(ipref == NULL) {
			rc = stk_destroy_sequence(seq);
			STK_CHECK(STKA_SVC,rc==STK_SUCCESS,"destroy the notification sequence : %d",rc);
			if(ipref) free(ipref);
			return STK_SUCCESS;
		}

		rc = stk_sga_add_service_state_to_sequence(seq,svc,state);
		STK_CHECK(STKA_SVC,rc==STK_SUCCESS,"add service state %d to sequence : %d",state,rc);
		if(rc != STK_SUCCESS) {
			rc = stk_destroy_sequence(seq);
			STK_CHECK(STKA_SVC,rc==STK_SUCCESS,"destroy the notification sequence : %d",rc);
			if(ipref) free(ipref);
			return STK_SUCCESS;
		}

		rc = stk_sga_add_service_state_name_to_sequence(seq,svc,state);
		STK_CHECK(STKA_SVC,rc==STK_SUCCESS,"add service state name for state %d to sequence : %d",state,rc);
		if(rc != STK_SUCCESS) {
			rc = stk_destroy_sequence(seq);
			STK_CHECK(STKA_SVC,rc==STK_SUCCESS,"destroy the notification sequence : %d",rc);
			if(ipref) free(ipref);
			return STK_SUCCESS;
		}

		if(svc->monitoring_df) {
			stk_ret getiprc = stk_data_flow_id_ip(svc->monitoring_df,(struct sockaddr *) ipref,sizeof(*ipref));
			STK_CHECK(STKA_SMB,getiprc==STK_SUCCESS,"Couldn't get IP for monitoring df %p",svc->monitoring_df);
			ipref->sin_addr.s_addr = htonl(ipref->sin_addr.s_addr);
			ipref->sin_port = htons(ipref->sin_port);
			STK_ASSERT(STKA_NET,1,"update IP address %x:%d to sequence %p", ntohl(ipref->sin_addr.s_addr), ntohs(ipref->sin_port), seq);

			rc = stk_data_flow_send(svc->monitoring_df,seq,0);
			if(rc != STK_SUCCESS) {
				STK_LOG(STK_LOG_ERROR,"send service state (%s,%d) to monitoring data flow (%p)",svc->name,state,svc->monitoring_df);
				STK_CHECK(STKA_SVC,stk_destroy_sequence(seq)==STK_SUCCESS,"destroy sequence %p while destroying service",seq);
				/* Continue to send to other data flows, and destroy the sequence gracefully */
			}
		}

		if(svc->monitoring_df != svc->notification_df && svc->notification_df) { /* No need to send to the same data flow */
			stk_ret getiprc = stk_data_flow_id_ip(svc->notification_df,(struct sockaddr *) ipref,sizeof(*ipref));
			STK_CHECK(STKA_SMB,getiprc==STK_SUCCESS,"Couldn't get IP for notification df %p",svc->notification_df);
			ipref->sin_addr.s_addr = htonl(ipref->sin_addr.s_addr);
			ipref->sin_port = htons(ipref->sin_port);
			STK_ASSERT(STKA_NET,1,"update IP address %x:%d to sequence %p", ntohl(ipref->sin_addr.s_addr), ntohs(ipref->sin_port), seq);

			rc = stk_data_flow_send(svc->notification_df,seq,0);
			if(rc != STK_SUCCESS) {
				STK_LOG(STK_LOG_ERROR,"send service state (%s,%d) to notification data flow (%p)",svc->name,state,svc->notification_df);
				STK_CHECK(STKA_SVC,stk_destroy_sequence(seq)==STK_SUCCESS,"destroy sequence %p while destroying service",seq);
				/* continue and destroy the sequence gracefully */
			}
		}

		/* free the data in the sequence */
		rc = stk_destroy_sequence(seq);
		STK_CHECK(STKA_SVC,rc==STK_SUCCESS,"destroy the notification sequence : %d",rc);

		if(ipref) free(ipref);
	}
	return STK_SUCCESS;
}

stk_uint32 stk_get_service_activity_tmo(stk_service_t *svc)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"get service activity timeout on a structure of type %d",svc->stct_type);
	return svc->activity_tmo;
}

stk_service_state stk_get_service_state(stk_service_t *svc)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"get service state on a structure of type %d",svc->stct_type);
	return svc->state;
}

char *stk_get_service_name(stk_service_t *svc)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"get service name on a structure of type %d",svc->stct_type);
	return svc->name;
}

stk_service_id stk_get_service_id(stk_service_t *svc)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"get service ID on a structure of type %d",svc->stct_type);
	return svc->id;
}

stk_service_type stk_get_service_type(stk_service_t *svc)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"get service type on a structure of type %d",svc->stct_type);
	return svc->type;
}

stk_data_flow_t *stk_get_monitoring_df(stk_service_t *svc)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"get service monitoring df on a structure of type %d",svc->stct_type);
	return svc->monitoring_df;
}

stk_data_flow_t *stk_get_notification_df(stk_service_t *svc)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"get service notification df on a structure of type %d",svc->stct_type);
	return svc->notification_df;
}

void stk_get_service_smartbeat(stk_service_t *svc,stk_smartbeat_t *smb)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"get  service smartbeat on a structure of type %d",svc->stct_type);
	memcpy(smb,&svc->smartbeat,sizeof(stk_smartbeat_t));
}

void stk_service_update_smartbeat(stk_service_t *svc,stk_smartbeat_t *smb)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"update service smartbeat on a structure of type %d",svc->stct_type);
	memcpy(&svc->smartbeat,smb,sizeof(svc->smartbeat));
}

void stk_service_update_smartbeat_checkpoint(stk_service_t *svc,stk_checkpoint_t checkpoint)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"update service checkpoint on a structure of type %d",svc->stct_type);
	svc->smartbeat.checkpoint = checkpoint;
}

stk_data_flow_t **stk_svc_get_smartbeat_flows(stk_service_t *svc)
{
	stk_data_flow_t **flows; int flow_cnt = 1;

	if(svc->monitoring_df) flow_cnt++;
	if(svc->notification_df) flow_cnt++;

	flows = calloc(sizeof(stk_data_flow_t *),flow_cnt);
	STK_CHECK(STKA_SVC,flows!=NULL,"Couldn't allocate memory for flows");
	if(flows == NULL) return NULL;

	flow_cnt = 0;
	if(svc->monitoring_df) flows[flow_cnt++] = svc->monitoring_df;
	if(svc->notification_df) flows[flow_cnt++] = svc->notification_df;
	return flows;
}

void stk_get_service_state_str(stk_service_t *svc,stk_service_state state,char *state_str,size_t size)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"get service state string on a structure of type %d for state %u",svc->stct_type,state);

	{
	stk_service_state_name *name_ptr = svc->state_names;

	if(!state_str || size == 0) return;

	while(name_ptr->state != STK_SERVICE_STATE_INVALID) {
		if(state == name_ptr->state) {
			strncpy(state_str,name_ptr->name,size);
			return;
		}

		name_ptr++;
	}

	sprintf(state_str,"%d",state);
	}
}

void stk_set_service_state_str(stk_service_t *svc,stk_service_state state,char *state_str,size_t size)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"set service state string on a structure of type %d for state %u string %s",svc->stct_type,state,state_str);

	{
	stk_service_state_name *name_ptr = svc->state_names;

	if(!state_str || size == 0) return;

	while(name_ptr->state != STK_SERVICE_STATE_INVALID) {
		if(state == name_ptr->state) {
			/* Replacement */
			if(svc->state_names == stk_service_state_default_names) {
				/* Alloc */
				svc->state_names = STK_ALLOC_BUF(sizeof(stk_service_state_default_names));
				STK_ASSERT(STKA_SVC,svc->state_names != NULL,"alloc state name table");

				memcpy(svc->state_names,stk_service_state_default_names,sizeof(stk_service_state_default_names));
				name_ptr = svc->state_names + (name_ptr - stk_service_state_default_names);
			}
			name_ptr->name = state_str;
			return;
		}

		name_ptr++;
	}

	/* New state name */
	if(svc->state_names == stk_service_state_default_names) {
		/* First new state - Alloc extended */
		svc->state_names_sz = sizeof(stk_service_state_default_names) + sizeof(stk_service_state_name);
		svc->state_names = STK_ALLOC_BUF(svc->state_names_sz);
		STK_ASSERT(STKA_SVC,svc->state_names != NULL,"alloc extended state name table");

		memcpy(svc->state_names,stk_service_state_default_names,sizeof(stk_service_state_default_names));
	} else {
		svc->state_names_sz += sizeof(stk_service_state_name);
		svc->state_names = STK_REALLOC(svc->state_names,svc->state_names_sz);
		STK_ASSERT(STKA_SVC,svc->state_names != NULL,"realloc state name table");
	}

	svc->state_names[(svc->state_names_sz/sizeof(stk_service_state_name))-2].state = state;
	svc->state_names[(svc->state_names_sz/sizeof(stk_service_state_name))-2].name = state_str;
	svc->state_names[(svc->state_names_sz/sizeof(stk_service_state_name))-1].state = STK_SERVICE_STATE_INVALID;
	svc->state_names[(svc->state_names_sz/sizeof(stk_service_state_name))-1].name = NULL;
	}
}

stk_env_t *stk_env_from_service(stk_service_t *svc)
{
	STK_ASSERT(STKA_SVC,svc->stct_type==STK_STCT_SERVICE,"get env from service on a structure of type %d",svc->stct_type);
	return svc->env;
}

stk_options_t *stk_get_service_options(stk_service_t *svc)
{
	STK_ASSERT(STKA_SVCGRP,svc!=NULL,"service group null or invalid :%p",svc);
	STK_ASSERT(STKA_SVCGRP,svc->stct_type==STK_STCT_SERVICE,"service %p passed in to stk_get_service_options is structure type %d",svc,svc->stct_type);

	return svc->options;
}

