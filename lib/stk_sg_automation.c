#include "stk_sg_automation_api.h"
#include "stk_internal.h"
#include "stk_service_api.h"
#include "stk_service_group_api.h"
#include "stk_env_api.h"
#include "stk_sequence_api.h"
#include "stk_sga_internal.h"
#include "stk_options_api.h"
#include "stk_data_flow_api.h"
#include "stk_name_service_api.h"
#include "stk_tcp.h"
#include <string.h>


typedef struct stk_sga_rcv_service_data_stct {
	stk_sga_service_inst_t inst;
	char *name;
	stk_service_state state;		/* Set non 0 in State updates */
	stk_smartbeat_svc_wire_t smartbeat_wire;
	char *state_name;
} stk_sga_rcv_service_data_t;

stk_ret stk_sga_sequence_cb(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	stk_sga_rcv_service_data_t *sd = (stk_sga_rcv_service_data_t *)clientd;

	switch(user_type) {
	case STK_STCT_SGA_SERVICE_INST:
		memcpy(&sd->inst,data,sz >= sizeof(stk_sga_service_inst_t) ? sizeof(stk_sga_service_inst_t) : sz);
		break;
	case STK_STCT_SGA_SERVICE_INST_NAME:
		{
		int slen = strlen(data) + 1;
		sd->name = malloc(slen);
		strcpy(sd->name,data);
		break;
		}
	case STK_STCT_SGA_SERVICE_STATE:
		sd->state = *((stk_service_state *) data);
		break;
	case STK_STCT_SGA_SERVICE_STATE_NAME:
		{
		int slen = strlen(data) + 1;
		sd->state_name = malloc(slen);
		strcpy(sd->state_name,data);
		break;
		}
	case STK_STCT_SVC_SMARTBEAT_WIRE:
		STK_ASSERT(STKA_HTTPD,sizeof(stk_smartbeat_svc_wire_t) == sz,"Received smartbeat is unsexpected size %ld",sz);
		memcpy(&sd->smartbeat_wire,data,sizeof(stk_smartbeat_svc_wire_t));
		break;
	}
	return STK_SUCCESS;
}

stk_ret stk_sga_invoke(stk_service_group_t *svcgrp, stk_sequence_t *seq)
{
	stk_service_t *svc;
	stk_ret rc;
	stk_sga_rcv_service_data_t sd;
	memset(&sd,0,sizeof(sd));

	switch(stk_get_sequence_id(seq))
	{
	case STK_SERVICE_NOTIF_CREATE:
	case STK_SERVICE_NOTIF_DESTROY:
	case STK_SERVICE_NOTIF_STATE:
	case STK_SMARTBEAT_SEQ:
		/* Valid SGA Sequences */
		break;
	default:
		return STK_SUCCESS;
	}

	rc = stk_iterate_sequence(seq,stk_sga_sequence_cb,&sd);
	STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"iterate over sequence %p being invoked",seq);

	switch(stk_get_sequence_id(seq))
	{
	case STK_SERVICE_NOTIF_CREATE:
		STK_ASSERT(STKA_SVCAUT,sd.inst.optype == STK_SGA_CREATE_SVC,"Expected STK_SGA_CREATE_SVC within STK_SERVICE_NOTIF_CREATE sequence");
 
		{
		char tmostr[50];
		stk_options_t *svcopts = stk_copy_extend_options(stk_get_service_group_options(svcgrp), 1);
		STK_ASSERT(STKA_SVCAUT,svcopts!=NULL,"realloc service options in stk_sga_invoke for service group %p",svcgrp);

		sprintf(tmostr,"%d",sd.inst.activity_tmo);

		rc = stk_append_option(svcopts, "activity_timeout", tmostr);
		STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"set the activity timeout for service group %p %d",svcgrp,sd.inst.activity_tmo);

		svc = stk_create_service(stk_get_service_group_env(svcgrp),sd.name,sd.inst.id, sd.inst.type, svcopts);
		STK_ASSERT(STKA_SVCAUT,svc!=NULL,"service creation failed for sequence %p",seq);

#if 0
		free(sd.name); /* stk_create_service dup's the name */
		sd.name = NULL;
#endif

		rc = stk_free_options(svcopts);
		STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"free service group %p options",svcgrp);

		stk_service_update_smartbeat_checkpoint(svc,sd.inst.smartbeat.checkpoint);

		/* Assume that the service is joined in the local group */
		{
		struct sockaddr_in client_ip;
		socklen_t addrlen = sizeof(client_ip);

		rc = stk_data_flow_client_ip(seq,&client_ip,&addrlen);
		STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"getting client ip, rc %d",rc);

		rc = stk_add_service_to_group(svcgrp,svc,client_ip,STK_SERVICE_IN_GROUP_JOINED);
		STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"setting service %x:%d state to joined failed %p %d",
			client_ip.sin_addr.s_addr, client_ip.sin_port, svc,sd.inst.state);
		}

		rc = stk_set_service_state(svc,sd.inst.state);
		STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"set service state %p %d",svc,sd.inst.state);
		}
		break;

	case STK_SERVICE_NOTIF_DESTROY:
		STK_ASSERT(STKA_SVCAUT,sd.inst.optype == STK_SGA_DESTROY_SVC,"Expected STK_SGA_DESTROY_SVC within STK_SERVICE_NOTIF_DESTROY sequence");

		{
		struct sockaddr_in client_ip;
		socklen_t addrlen = sizeof(client_ip);

		rc = stk_data_flow_client_ip(seq,&client_ip,&addrlen);
		STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"getting client ip, rc %d",rc);

		svc = stk_find_service_in_group_by_name(svcgrp,sd.name,client_ip);
		if(svc) {
			char *name = (char *) stk_get_service_name(svc);

			rc = stk_remove_service_from_group(svcgrp,svc);
			STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"remove service %p from service group",svc);

			stk_service_update_smartbeat(svc,&sd.inst.smartbeat);

			rc = stk_destroy_service(svc,NULL);
			STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"destroy service %p %d",svc,sd.inst.state);
		}
		}
		break;

	case STK_SMARTBEAT_SEQ:
		{
			struct sockaddr_in client_ip;
			socklen_t addrlen = sizeof(client_ip);
			stk_ret rc;

			rc = stk_data_flow_client_ip(seq,&client_ip,&addrlen);
			STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"getting client ip, rc %d",rc);

			rc = stk_service_group_handle_smartbeat(svcgrp,sd.smartbeat_wire.service,&sd.smartbeat_wire.smartbeat,client_ip);
			STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"handle smartbeat service group %p",svcgrp);
		}
		break;
	}

	switch(stk_get_sequence_id(seq))
	{
	case STK_SERVICE_NOTIF_CREATE:
	case STK_SERVICE_NOTIF_DESTROY:
	case STK_SERVICE_NOTIF_STATE:
		/* state is set in create, destroy or state sequences */
		if(sd.state != STK_SERVICE_STATE_INVALID) {
			struct sockaddr_in client_ip;
			socklen_t addrlen = sizeof(client_ip);

			rc = stk_data_flow_client_ip(seq,&client_ip,&addrlen);
			STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"getting client ip, rc %d",rc);

			svc = stk_find_service_in_group_by_name(svcgrp,sd.name,client_ip);
			if(svc) {
				/* Determine if this service needs to add this state name (if provided) to the service table before
				 * setting state which might call an app callback
				 */
				if(sd.state_name)
				{
					char state_name[STK_SERVICE_STATE_NAME_MAX];

					stk_get_service_state_str(svc,sd.state,state_name,sizeof(state_name));
					if(strcasecmp(state_name,sd.state_name) != 0) {
						STK_DEBUG(STKA_SVCAUT,"Updating service %p %s state %d string %s",svc,stk_get_service_name(svc),sd.state,state_name);
						stk_set_service_state_str(svc,sd.state,sd.state_name,sizeof(state_name));
						sd.state_name = NULL;
					}
				}

				rc = stk_set_service_state(svc,sd.state);
				STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"set service state %p %d",svc,sd.inst.state);
			}
		}
	}
	if(sd.name) free(sd.name);
	if(sd.state_name) free(sd.state_name);

	return STK_SUCCESS;
}

stk_ret stk_sga_add_service_op_to_sequence(stk_sequence_t *seq,stk_service_t *svc,stk_uint32 optype)
{
	stk_sga_service_inst_t svcinst;
	char *c;
	stk_ret rc;

	/* Format buffer */
	memset(&svcinst,0,sizeof(svcinst)); /* avoid valgrind errors with sendmsg - could be optimized */
	svcinst.sz = sizeof(svcinst);
	svcinst.id = stk_get_service_id(svc);
	svcinst.type = stk_get_service_type(svc);
	svcinst.state = stk_get_service_state(svc);
	svcinst.activity_tmo = stk_get_service_activity_tmo(svc);
	stk_get_service_smartbeat(svc,&svcinst.smartbeat);
	svcinst.optype = optype;

	rc = stk_copy_to_sequence(seq,&svcinst,sizeof(svcinst), STK_STCT_SGA_SERVICE_INST);
	c = (char *) stk_get_service_name(svc);
	STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"copy service data to sequence %p for service name '%s'",seq,c ? c : "");

	rc = stk_sga_add_service_name_to_sequence(seq,svc);
	STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"copy service name '%s' to sequence %p",c ? c : "",seq);

	return STK_SUCCESS;
}

stk_ret stk_sga_add_service_state_to_sequence(stk_sequence_t *seq,stk_service_t *svc,stk_service_state state)
{
	stk_ret rc;

	rc = stk_copy_to_sequence(seq,&state,sizeof(state),STK_STCT_SGA_SERVICE_STATE);
	STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"copy service state %d to sequence %p",state,seq);
	return STK_SUCCESS;
}

stk_ret stk_sga_add_service_state_name_to_sequence(stk_sequence_t *seq,stk_service_t *svc,stk_service_state state)
{
	stk_ret rc;
	char state_str[STK_SERVICE_STATE_NAME_MAX];

	stk_get_service_state_str(svc,state,state_str,sizeof(state_str));

	rc = stk_copy_to_sequence(seq,state_str,strlen(state_str)+1,STK_STCT_SGA_SERVICE_STATE_NAME);
	STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"copy service state name to sequence %p for service %p name %s state %s",seq,svc,
		stk_get_service_name(svc) ? stk_get_service_name(svc) : "", state_str);

	return STK_SUCCESS;
}

stk_ret stk_sga_add_service_name_to_sequence(stk_sequence_t *seq,stk_service_t *svc)
{
	char *svcname;
	stk_ret rc;

	svcname = (char *) stk_get_service_name(svc);
	STK_ASSERT(STKA_SVCAUT,svcname!=NULL,"Service with no name passed to stk_sga_add_service_name_to_sequence() for sequence %p",seq);

	rc = stk_copy_to_sequence(seq,svcname,strlen(svcname)+1,STK_STCT_SGA_SERVICE_INST_NAME);
	STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"copy service name to sequence %p for service name %s",seq,svcname ? svcname : "");

	return STK_SUCCESS;
}

struct sockaddr_in * stk_sga_add_service_reporting_ip_ref(stk_sequence_t *seq,stk_data_flow_t *df)
{
	stk_ret rc;
	struct sockaddr_in *ipaddr = malloc(sizeof(struct sockaddr_in));
	if(!ipaddr) return NULL;

	rc = stk_data_flow_id_ip(df,(struct sockaddr *) ipaddr,(socklen_t) sizeof(*ipaddr));
	STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"get reporting IP for data flow %p",df);

	STK_DEBUG(STKA_SVCAUT,"REPORTING df %p IP %x:%d REF",df,ipaddr->sin_addr.s_addr,ipaddr->sin_port);
	ipaddr->sin_addr.s_addr = htonl(ipaddr->sin_addr.s_addr);
	ipaddr->sin_port = htons(ipaddr->sin_port);

	rc = stk_add_reference_to_sequence(seq,ipaddr,sizeof(*ipaddr),STK_STCT_SGA_SERVICE_IP_ID);
	STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"copy IP address %x:%d to sequence %p",
		ntohl(ipaddr->sin_addr.s_addr), ntohs(ipaddr->sin_port), seq);

	return (struct sockaddr_in *) ipaddr;
}

void stk_sga_name_info_cb(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info,stk_name_info_cb_type cb_type)
{
	if(cb_type == STK_NS_REQUEST_EXPIRED) {
		STK_LOG(STK_LOG_NORMAL,"Service Automation received name callback for '%s' expired",name_info->name);
		return;
	}

	STK_LOG(STK_LOG_NORMAL,"Service Automation received name callback for '%s' type %d",name_info->name,cb_type);
}

stk_ret stk_sga_register_group_name(stk_service_group_t *svcgrp,stk_data_flow_t *df)
{
	struct sockaddr_in data_flow_ip;
	stk_ret rc;

	rc = stk_data_flow_id_ip(df,(struct sockaddr *) &data_flow_ip,sizeof(data_flow_ip));
	STK_CHECK(STKA_SVCGRP,rc==STK_SUCCESS,"get listening IP on df %p",df);
	if(rc == STK_SUCCESS && data_flow_ip.sin_addr.s_addr != 0) {
		stk_options_t name_options[] = {
			{ "destination_address", NULL}, {"destination_port", NULL}, {"destination_protocol", NULL},
			{ "meta_data_sequence", NULL}, { NULL, NULL }
		};
		char *group_name = stk_get_service_group_name(svcgrp);
		stk_uint16 flow_type = stk_get_data_flow_type(df);
		char port_str[6],*ip_str;
		stk_sequence_t *meta_data_seq = NULL;
		stk_env_t *stkbase = stk_get_service_group_env(svcgrp);

		sprintf(port_str,"%d",htons(data_flow_ip.sin_port));
		ip_str = inet_ntoa(data_flow_ip.sin_addr);

		STK_LOG(STK_LOG_NORMAL,"Registering group name '%s' with %s:%s",group_name,ip_str,port_str);

		meta_data_seq = stk_create_sequence(stkbase,"meta data sequence",0,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
		STK_ASSERT(STKA_SVCAUT,meta_data_seq!=NULL,"Failed to allocate meta data sequence");

		name_options[0].data = ip_str;
		name_options[1].data = port_str;
		name_options[2].data = stk_data_flow_protocol(df);
		name_options[3].data = meta_data_seq;

		rc = stk_copy_to_sequence(meta_data_seq,&flow_type,sizeof(flow_type), STK_MD_DATA_FLOW_TYPE);
		STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"copy data flow type %d to meta data",flow_type);

		/* Register Service Group Name */
		if(group_name) {
			rc = stk_register_name(stk_env_get_name_service(stkbase),
				group_name, 5 /* linger 5 secs */, 10000, stk_sga_name_info_cb, NULL, name_options);
			STK_ASSERT(STKA_SVCAUT,rc==STK_SUCCESS,"register name rc %d",rc);
		}
	}
	return STK_SUCCESS;
}

