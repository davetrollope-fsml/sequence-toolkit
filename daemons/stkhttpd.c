
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
#include "stk_data_flow_api.h"
#include "stk_sg_automation_api.h"
#include "stk_sga_internal.h"				/* Shared structures!!! */
#include "stk_timer_api.h"
#include "stk_sync_api.h"
#include "stk_name_service_api.h"
#include "stkhttpd.h"						/* Shared structures!!! */
#include "stk_ids.h"
#include "PLists.h"
#include "stk_httpcontent.h"
#include "../examples/eg_dispatcher_api.h"

/* Use internal header for asserts */
#include "stk_internal.h"
#include "stk_sync.h"
#include "stk_ports.h"
#include "mongoose.h"

extern char content[];

stk_mutex_t *service_list_lock;
stk_mutex_t *service_history_lock;
List *service_list;
List *service_history; /* Removed Services */
int service_hist_max = 100,service_hist_size;
stk_timer_set_t *httpd_timers;
stk_timer_t *update_svc_timer;
stk_timer_t *registration_timer;
stk_dispatcher_t *httpd_dispatcher;

typedef char data_flow_id_str[64];
data_flow_id_str rcv_df_ids[3];
int rcv_dfs_registered;

typedef struct cmdipopts {
	char *ip;
	char *port;
} ipopts;

/* command line options provided - set in process_cmdline() */
static struct cmdopts {
	char *http_port;
	ipopts multicast;
	ipopts unicast;
	ipopts tcp;
	int linger;
	int cbs;
	char *name_server_ip;
	char *name_server_port;
	char name_server_protocol[5];
	char group_name[STK_MAX_GROUP_NAME_LEN];
	char monitor_name[STK_MAX_GROUP_NAME_LEN];
} opts;

extern struct stk_url_matches urlroutes[];
extern struct stk_url_prefixes urlproutes[];
extern int stk_url_notfound(const struct mg_request_info *request_info);

int stopped = 0;
void stkhttpd_term(int signum)
{
	printf("stkhttpd received SIGTERM/SIGINT, exiting...\n");
	stop_dispatching(httpd_dispatcher);
	stopped=1;
}

static void *httpd_callback(enum mg_event event, struct mg_connection *conn)
{
	const struct mg_request_info *request_info = mg_get_request_info(conn);

	if (event == MG_NEW_REQUEST) {
		int content_length = 0,idx;

		for(idx = 0; urlroutes[idx].match != NULL; idx++) {
			if(strcasecmp(urlroutes[idx].match,request_info->uri) == 0) {
				content_length = urlroutes[idx].fptr(request_info);
				break;
			}
		}
		if(content_length == 0) {
			for(idx = 0; urlproutes[idx].prefix != NULL; idx++) {
				if(strncasecmp(urlproutes[idx].prefix,request_info->uri,urlproutes[idx].len) == 0) {
					if(urlproutes[idx].fptr == NULL) return NULL; /* Pass through to mongoose */

					content_length = urlproutes[idx].fptr(request_info);
					break;
				}
			}
		}

		if(content_length == 0)
			content_length = stk_url_notfound(request_info);

		mg_printf(conn,
			"HTTP/1.1 200 OK\r\n"
			"Content-Type: text/html\r\n"
			"Content-Length: %d\r\n"        // Always set Content-Length
			"\r\n"
			"%s", content_length, content);
		// Mark as processed
		return "";
	} else {
		return NULL;
	}
}

void free_collected_service_data(stk_collect_service_data_t *svcdata)
{
	if(svcdata->svc_name) { free(svcdata->svc_name); svcdata->svc_name = NULL; }
	if(svcdata->state_name) { free(svcdata->state_name); svcdata->state_name = NULL; }
}

void free_collected_service_data_node(Node *n)
{
	free_collected_service_data(NodeData(n));
	FreeNode(n);
}

stk_ret process_seq_segment(stk_sequence_t *seq, void *vdata, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	stk_collect_service_data_t *svcdata = (stk_collect_service_data_t *) clientd;

	STK_DEBUG(STKA_HTTPD,"Sequence %p Received %ld bytes of type %lx",seq,sz,user_type);
	switch(user_type) {
	case STK_STCT_SGA_SERVICE_INST:
		if(vdata) {
			stk_sga_service_inst_t *svc_inst = (stk_sga_service_inst_t *) vdata;

			STK_DEBUG(STKA_HTTPD,"Service ID %ld type %d state %d Operation type %d checkpoint %ld",svc_inst->id,svc_inst->type,svc_inst->state,svc_inst->optype,svc_inst->smartbeat.checkpoint);
			STK_ASSERT(STKA_HTTPD,svc_inst->sz >= sizeof(stk_sga_service_inst_t),"Wire data for service instance smaller (%d) than expected (%ld) - version mismatch?",svc_inst->sz,sizeof(stk_sga_service_inst_t));

			memcpy(&svcdata->svcinst,svc_inst,sizeof(svcdata->svcinst));
		}
		break;
	case STK_STCT_SGA_SERVICE_INST_NAME:
		if(vdata)
			svcdata->svc_name = strdup(vdata);
		break;
	case STK_STCT_SGA_SERVICE_STATE: {
		if(vdata)
			memcpy(&svcdata->state_update,vdata,sz);
		break;
		}
	case STK_STCT_SGA_SERVICE_STATE_NAME:
		if(vdata)
			svcdata->state_name = strdup(vdata);
		break;
	case STK_STCT_SVC_SMARTBEAT_WIRE:
		{
		stk_smartbeat_svc_wire_t *wire_smartbeat = (stk_smartbeat_svc_wire_t *) vdata;
		stk_ret lockret;
		int found = STK_FALSE;

		STK_ASSERT(STKA_HTTPD,sizeof(stk_smartbeat_svc_wire_t) == sz,"Received smartbeat is expected size? %ld (should be %ld)",sz,sizeof(stk_smartbeat_svc_wire_t));

		STK_DEBUG(STKA_HTTPD,"SMARTBEAT service %lx svcdata %p rcvd ip %x:%d:%p", wire_smartbeat->service, svcdata, svcdata->ipaddr.sin_addr.s_addr, svcdata->ipaddr.sin_port, svcdata->svc_grp_name);

		lockret = stk_mutex_lock(service_list_lock);
		STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service list lock");

		if(!IsPListEmpty(service_list)) {
			for(Node *n = FirstNode(service_list); !AtListEnd(n); n = NxtNode(n)) {
				stk_collect_service_data_t *saved_svcdata = NodeData(n);

				STK_ASSERT(STKA_HTTPD,saved_svcdata!=NULL,"svcdata %p saved %p rcvd ip %x:%d:%p saved ip %x:%d:%p rcvd service ID %ld saved ID %ld",
					svcdata,saved_svcdata,
					svcdata->ipaddr.sin_addr.s_addr, svcdata->ipaddr.sin_port, svcdata->svc_grp_name,
					saved_svcdata->ipaddr.sin_addr.s_addr, saved_svcdata->ipaddr.sin_port, saved_svcdata->svc_grp_name,
					wire_smartbeat->service,saved_svcdata->svcinst.id);

				if(svcdata->ipaddr.sin_addr.s_addr == saved_svcdata->ipaddr.sin_addr.s_addr && /* Check IP/Port matches */
				   svcdata->ipaddr.sin_port == saved_svcdata->ipaddr.sin_port &&
				   wire_smartbeat->service == saved_svcdata->svcinst.id) {
					STK_ASSERT(STKA_HTTPD,saved_svcdata!=NULL,"matched record - copying smartbeat %p",saved_svcdata);
					saved_svcdata->svcinst.smartbeat = wire_smartbeat->smartbeat;
					memcpy(&saved_svcdata->rcv_time,&((stk_collect_service_data_t*)clientd)->rcv_time,sizeof(saved_svcdata->rcv_time));
					found = STK_TRUE;
				}
			}
		}

		lockret = stk_mutex_unlock(service_list_lock);
		STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service list unlock");

		lockret = stk_mutex_lock(service_history_lock);
		STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service history lock");

		if(found == STK_FALSE && !IsPListEmpty(service_history)) {
			/* Might need to resurrect this guy - perhaps the smartbeat was delayed in the network */
			for(Node *n = FirstNode(service_history); !AtListEnd(n); n = NxtNode(n)) {
				stk_collect_service_data_t *saved_svcdata = NodeData(n);

				STK_ASSERT(STKA_HTTPD,saved_svcdata!=NULL,"svcdata %p saved %p rcvd ip %x:%d:%p saved ip %x:%d:%p rcvd service ID %ld saved ID %ld",
					svcdata,saved_svcdata,
					svcdata->ipaddr.sin_addr.s_addr, svcdata->ipaddr.sin_port, svcdata->svc_grp_name,
					saved_svcdata->ipaddr.sin_addr.s_addr, saved_svcdata->ipaddr.sin_port, saved_svcdata->svc_grp_name,
					wire_smartbeat->service,saved_svcdata->svcinst.id);

				if(svcdata->ipaddr.sin_addr.s_addr == saved_svcdata->ipaddr.sin_addr.s_addr && /* Check IP/Port matches */
				   svcdata->ipaddr.sin_port == saved_svcdata->ipaddr.sin_port &&
				   wire_smartbeat->service == saved_svcdata->svcinst.id) {
					/* Resurrect this entry */
					STK_ASSERT(STKA_HTTPD,saved_svcdata!=NULL,"matched record - copying smartbeat %p and resurrecting",saved_svcdata);
					saved_svcdata->svcinst.smartbeat = wire_smartbeat->smartbeat;
					memcpy(&saved_svcdata->rcv_time,&((stk_collect_service_data_t*)clientd)->rcv_time,sizeof(saved_svcdata->rcv_time));
					Remove(n);
					saved_svcdata->inactivity = 0;
					AddTail(service_list,n);
				}
			}
		}

		lockret = stk_mutex_unlock(service_history_lock);
		STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service history unlock");
		}
		break;
#if 0
	case STK_STCT_SGA_SERVICE_IP_ID:
		if(vdata) {
			struct sockaddr_in ipaddr;
			memcpy(&ipaddr,vdata,sizeof(svcdata->ipaddr));
			ipaddr.sin_port = ntohs(ipaddr.sin_port);
			ipaddr.sin_addr.s_addr = ntohl(ipaddr.sin_addr.s_addr);
			STK_ASSERT(STKA_HTTPD,svcdata!=NULL,"svcdata %p IP %x:%d (%d)",
				svcdata, ipaddr.sin_addr.s_addr, ipaddr.sin_port, ntohs(ipaddr.sin_port));
			memcpy(&svcdata->ipaddr,&ipaddr,sizeof(svcdata->ipaddr));
		}
		break;
#endif
	case STK_SERVICE_GROUP_NAME:
		if(vdata)
			svcdata->svc_grp_name = strdup(vdata);
		break;
	}

	return STK_SUCCESS;
}

extern stk_smartbeat_t daemon_smb;
int name_cbs_rcvd;
static void process_data(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc;
	stk_collect_service_data_t *svcdata;
	stk_sequence_id sid = stk_get_sequence_id(rcv_seq);
	stk_sequence_type stype = stk_get_sequence_type(rcv_seq);

	rc = stk_smartbeat_update_current_time(&daemon_smb);
	STK_ASSERT(STKA_HTTPD,rc == STK_SUCCESS,"get current time");

	if(stype == STK_SEQUENCE_TYPE_REQUEST) return;

	if(stype != STK_SEQUENCE_TYPE_MGMT) {
		STK_LOG(STK_LOG_NORMAL,"Received unexpected sequence type %d",stk_get_sequence_type(rcv_seq));
		return;
	}

	{
	Node *n = NewDataNode(sizeof(stk_collect_service_data_t)); /* Allocs cleared data */
	int freecnode = 1;

	STK_ASSERT(STKA_MEM,n!=NULL,"allocate service data structure");
	svcdata = NodeData(n);
	STK_ASSERT(STKA_HTTPD,svcdata!=NULL,"get svcdata from allocated node");

	rc = stk_smartbeat_update_current_time(&svcdata->rcv_time);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"get current time");

	{
	socklen_t addrlen = sizeof(svcdata->ipaddr);
	stk_uint64 protocol_len = sizeof(svcdata->client_protocol);

	rc = stk_data_flow_client_ip(rcv_seq,&svcdata->ipaddr,&addrlen);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"getting client ip %x rc %d",svcdata->ipaddr.sin_addr.s_addr,rc);

	rc = stk_data_flow_client_protocol(rcv_seq,svcdata->client_protocol,&protocol_len);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"getting client protocol rc %d",rc);
	}

	/* Call process_seq_segment() on each element in the sequence */
	rc = stk_iterate_sequence(rcv_seq,process_seq_segment,svcdata);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"process received sequence buffer space to receive");


	switch(stk_get_sequence_id(rcv_seq))
	{
	case STK_SERVICE_NOTIF_CREATE:
		{
		stk_ret lockret = stk_mutex_lock(service_list_lock);
		STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service list lock");

		if(!IsPListEmpty(service_list)) {
			/* find and remove then add */
			for(Node *rn = FirstNode(service_list); !AtListEnd(rn); rn = NxtNode(rn)) {
				stk_collect_service_data_t *saved_svcdata = (stk_collect_service_data_t *) NodeData(rn);
				struct sockaddr_in *saved_addr = (struct sockaddr_in *) &saved_svcdata->ipaddr;
				struct sockaddr_in *rcvd_addr = (struct sockaddr_in *) &svcdata->ipaddr;

				STK_ASSERT(STKA_HTTPD,saved_svcdata!=NULL,"create svcdata %p saved %p rcvd ip %x:%d:%p saved ip %x:%d:%p",
					svcdata,saved_svcdata,
					svcdata->ipaddr.sin_addr.s_addr, svcdata->ipaddr.sin_port, svcdata->svc_grp_name,
					saved_svcdata->ipaddr.sin_addr.s_addr, saved_svcdata->ipaddr.sin_port, saved_svcdata->svc_grp_name);

				STK_DEBUG(STKA_HTTPD,"CREATE svcdata %p rcvd ip %x:%d:%p", svcdata, svcdata->ipaddr.sin_addr.s_addr, svcdata->ipaddr.sin_port, svcdata->svc_grp_name);
				if(saved_addr->sin_addr.s_addr == rcvd_addr->sin_addr.s_addr && /* Check IP/Port matches */
				   saved_addr->sin_port == rcvd_addr->sin_port &&
				   saved_svcdata->svcinst.id == svcdata->svcinst.id && /* Check service ID/type matches */
				   saved_svcdata->svcinst.type == svcdata->svcinst.type)
				{
					Remove(rn);
					saved_svcdata->displaced = 1; /* Record that we forcibly moved this because of a new registration */

					lockret = stk_mutex_lock(service_history_lock);
					STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service history lock");

					/* Move to history */
					AddHead(service_history,rn);

					lockret = stk_mutex_unlock(service_history_lock);
					STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service history unlock");

					break;
				}
			}
		}
		else {
			STK_ASSERT(STKA_HTTPD,svcdata!=NULL,"create svcdata %p rcvd ip %x:%d:%p",
				svcdata, svcdata->ipaddr.sin_addr.s_addr, svcdata->ipaddr.sin_port, svcdata->svc_grp_name);
		}
		AddTail(service_list,n);

		lockret = stk_mutex_unlock(service_list_lock);
		STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service list unlock");

		freecnode = 0;
		}
		break;
	case STK_SERVICE_NOTIF_DESTROY:
		{
		List *l = service_list;
		stk_ret lockret = stk_mutex_lock(service_list_lock);
		STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service list lock");
		
		while(l) {
			if(!IsPListEmpty(l)) {
				/* find and remove */
				for(Node *rn = FirstNode(l); !AtListEnd(rn); rn = NxtNode(rn)) {
					stk_collect_service_data_t *saved_svcdata = (stk_collect_service_data_t *) NodeData(rn);
					struct sockaddr_in *saved_addr = (struct sockaddr_in *) &saved_svcdata->ipaddr;
					struct sockaddr_in *rcvd_addr = (struct sockaddr_in *) &svcdata->ipaddr;

					STK_DEBUG(STKA_HTTPD,"DESTROY svcdata %p rcvd ip %x:%d:%p", svcdata, svcdata->ipaddr.sin_addr.s_addr, svcdata->ipaddr.sin_port, svcdata->svc_grp_name);
					if(saved_addr->sin_addr.s_addr == rcvd_addr->sin_addr.s_addr && /* Check IP/Port matches */
					   saved_addr->sin_port == rcvd_addr->sin_port &&
					   saved_svcdata->svcinst.id == svcdata->svcinst.id && 
					   saved_svcdata->svcinst.type == svcdata->svcinst.type) {

						if(saved_svcdata->svcinst.state != svcdata->svcinst.state && saved_svcdata->state_name) {
							free(saved_svcdata->state_name);
							saved_svcdata->state_name = NULL;
						}
						if(svcdata->state_name) {
							saved_svcdata->state_name = svcdata->state_name;
							svcdata->state_name = NULL;
						}
						memcpy(&saved_svcdata->svcinst,&svcdata->svcinst,sizeof(svcdata->svcinst));

						if(svcdata->svcinst.state != STK_SERVICE_STATE_INVALID)
							saved_svcdata->svcinst.state = svcdata->svcinst.state;

						if(saved_svcdata->svcinst.state == STK_SERVICE_STATE_TIMED_OUT)
							saved_svcdata->inactivity = 1; /* Record that this was inactive */

						saved_svcdata->svcinst.smartbeat.checkpoint = svcdata->svcinst.smartbeat.checkpoint;

						if(l == service_list) {
							/* Move to history */
							Remove(rn);

							lockret = stk_mutex_lock(service_history_lock);
							STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service history lock");

							AddHead(service_history,rn);
							if(service_hist_size < service_hist_max)
								service_hist_size++;
							else {
								rn = FirstNode(service_history);
								Remove(rn);
								free_collected_service_data_node(rn);
							}

							lockret = stk_mutex_unlock(service_history_lock);
							STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service history unlock");
						} else {
							/*
							 * Its possible that an entry was marked inactive by timing out before the destroy arrived,
							 * so lets clear that because its no longer true - we heard from the service, even if it
							 * was delayed.. Except of course when the state is set to timed out!!
							 */
							if(saved_svcdata->svcinst.state != STK_SERVICE_STATE_TIMED_OUT)
								saved_svcdata->inactivity = 0;
						}
						break;
					}
				}
			}
			if (l == service_history) l = NULL;
			if (l == service_list) l = service_history;
		}
		lockret = stk_mutex_unlock(service_list_lock);
		STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service list unlock");
		}
		break;
	}

	switch(stk_get_sequence_id(rcv_seq))
	{
	case STK_SERVICE_NOTIF_CREATE:
	case STK_SERVICE_NOTIF_DESTROY:
	case STK_SERVICE_NOTIF_STATE:
		{
		stk_ret lockret = stk_mutex_lock(service_list_lock);
		STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service list lock");

		if(svcdata->state_update != STK_SERVICE_STATE_INVALID) {
			if(!IsPListEmpty(service_list)) {
				for(Node *rn = FirstNode(service_list); !AtListEnd(rn); rn = NxtNode(rn)) {
					stk_collect_service_data_t *saved_svcdata = (stk_collect_service_data_t *) NodeData(rn);
					if(svcdata->ipaddr.sin_addr.s_addr == saved_svcdata->ipaddr.sin_addr.s_addr && /* Check IP/Port matches */
					   svcdata->ipaddr.sin_port == saved_svcdata->ipaddr.sin_port &&
					   strcasecmp(saved_svcdata->svc_name,svcdata->svc_name) == 0) {
						/* Matched service instance - update state and state name */
						saved_svcdata->svcinst.state = svcdata->state_update;
						if(saved_svcdata->state_name) free(saved_svcdata->state_name);
						saved_svcdata->state_name = svcdata->state_name;
						svcdata->state_name = NULL;
						break;
					}
				}
			}
		}

		lockret = stk_mutex_unlock(service_list_lock);
		STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service list unlock");
		}
		break;
	}

	if(freecnode)
		free_collected_service_data_node(n);

	}
}

stk_bool stk_service_has_timed_out(stk_collect_service_data_t *svcdata)
{
	struct timeval tv;

	return stk_has_smartbeat_timed_out(&svcdata->rcv_time,&daemon_smb,svcdata->svcinst.activity_tmo);
}

stk_bool stk_timeout_service(stk_collect_service_data_t *svcdata,Node *n)
{
	if(stk_service_has_timed_out(svcdata)) {
		stk_ret lockret;
		STK_LOG(STK_LOG_NORMAL,"service group %s timed out",svcdata->svc_grp_name ? svcdata->svc_grp_name : "(unknown)");

		lockret = stk_mutex_lock(service_history_lock);
		STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service history lock");

		/* move to history */
		Remove(n);
		svcdata->inactivity = 1; /* Record that we forcibly moved this because it was inactive */
		AddHead(service_history,n);

		lockret = stk_mutex_unlock(service_history_lock);
		STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service history unlock");

		return STK_TRUE;
	}
	return STK_FALSE;
}

void name_info_cb(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info,stk_name_info_cb_type cb_type)
{
	name_cbs_rcvd++; /* Name server response */
}

stk_ret stk_register_monitor_addresses(stk_env_t *stkbase,char *tcp_id, char *udp_id, char *mcast_id)
{
	/* This is registering 127.0.0.1:20003 as the IP/Port associated with this name - its not the server we are connecting to */
	stk_options_t name_options[] = { { NULL, NULL }, { NULL, NULL } };
	stk_sequence_t *meta_data_seq = NULL;
	stk_ret rc;

	meta_data_seq = stk_create_sequence(stkbase,"meta data sequence",0,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
	STK_ASSERT(STKA_HTTPD,meta_data_seq!=NULL,"allocate meta data sequence");
	opts.cbs++;

	rc = stk_copy_to_sequence(meta_data_seq,tcp_id,strlen(tcp_id) + 1, STK_MD_HTTPD_TCP_ID);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"copy tcp id to meta data sequence");

	if(udp_id[0]) {
		rc = stk_copy_to_sequence(meta_data_seq,udp_id,strlen(udp_id) + 1, STK_MD_HTTPD_UDP_ID);
		STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"copy udp id to meta data sequence");
	}

	if(mcast_id[0]) {
		rc = stk_copy_to_sequence(meta_data_seq,mcast_id,strlen(mcast_id) + 1, STK_MD_HTTPD_MCAST_ID);
		STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"copy mcast id to meta data sequence");
	}

	/* Overwrite the NULL entry with the meta data sequence option */
	name_options[0].name = "meta_data_sequence";
	name_options[0].data = (void *) meta_data_seq;

	/* printf ("Registering IDs on name %s\n",opts.monitor_name); */

	rc = stk_register_name(stk_env_get_name_service(stkbase), opts.monitor_name, opts.linger, 10000, name_info_cb, NULL, name_options);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"register name rc %d",rc);

	rc = stk_destroy_sequence(meta_data_seq);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"destroy the meta data sequence rc %d",rc);

	/* Dispatch waiting for response */
	while(name_cbs_rcvd < opts.cbs) {
		client_dispatcher_timed(httpd_dispatcher,stkbase,NULL,1000);
	}

	return STK_SUCCESS;
}

void reregister_name_timer_cb(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type)
{
	if(cb_type != STK_TIMER_CANCELLED) {
		stk_ret ret = stk_register_monitor_addresses(stk_env_from_timer_set(timer_set),rcv_df_ids[2],rcv_df_ids[1],rcv_df_ids[0]);
		STK_ASSERT(STKA_HTTPD,ret==STK_SUCCESS,"register monitoring addresses");
		ret = stk_reschedule_timer(timer_set,timer);
		STK_ASSERT(STKA_HTTPD,ret==STK_SUCCESS,"reschedule registration timer");
	}
}

void update_svc_timer_cb(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type)
{
	stk_ret rc,lockret;

	rc = stk_smartbeat_update_current_time(&daemon_smb);
	STK_ASSERT(STKA_HTTPD,rc == STK_SUCCESS,"get current time");

	lockret = stk_mutex_lock(service_list_lock);
	STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service list lock");

	if(!IsPListEmpty(service_list)) {
		Node *on = NULL;
		for(Node *n = FirstNode(service_list); !AtListEnd(n); n = on) {
			stk_collect_service_data_t *svcdata = (stk_collect_service_data_t *) NodeData(n);
			on = NxtNode(n); /* Save next pointer in case n is removed */
			stk_timeout_service(svcdata,n);
		}
	}

	lockret = stk_mutex_unlock(service_list_lock);
	STK_ASSERT(STKA_HTTPD,lockret==STK_SUCCESS,"service list unlock");

	if(cb_type != STK_TIMER_CANCELLED) {
		rc = stk_reschedule_timer(timer_set,timer);
		STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"reschedule periodic timer");
	}
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
 
/*
 * Whats the best way to register an IP and Port for each protocol with the name server?
 * Also, want a default with a simple name?
 * Should we just register the protocol string?
 * Perhaps a single name with multiple strings? ***
 */
void stk_httpd_usage()
{
	fprintf(stderr,"Usage: stkhttpd [options]\n");
	fprintf(stderr,"       -h                        : This help!\n");
	fprintf(stderr,"       -p <port>                 : HTTP Port\n");
	fprintf(stderr,"       -R [protocol:]<ip[:port]> : IP and port of name server\n");
	fprintf(stderr,"       -N <name>                 : name to register for this service (default monitor)\n");
	fprintf(stderr,"       -M <-|ip[:port]>          : Multicast Listening IP and port (default 224.10.10.20:20001)\n");
	fprintf(stderr,"       -U <-|ip[:port]>          : UDP [unicast] Listening IP and port (default 127.0.0.1:20001)\n");
	fprintf(stderr,"       -T <-|ip[:port]>          : TCP Listening IP and port (default 127.0.0.1:20001)\n");
	fprintf(stderr,"       -L <seconds>              : Linger time for names after death\n");
}

static int process_cmdline(int argc,char *argv[],struct cmdopts *opts )
{
	int rc;

	while(1) {
		rc = getopt(argc, argv, "hp:N:R:M:U:T:L:");
		if(rc == -1) return 0;

		switch(rc) {
		case 'h': /* Help! */
			stk_httpd_usage();
			exit(0);

		case 'R': /* Set the IP/Port of the Name Server */
			{
			stk_protocol_def_t pdef;
			stk_data_flow_parse_protocol_str(&pdef,optarg);

			if(pdef.protocol[0] != '\0') strcpy(opts->name_server_protocol,pdef.protocol);
			if(pdef.ip[0] != '\0') opts->name_server_ip = strdup(pdef.ip);
			if(pdef.port[0] != '\0') opts->name_server_port = strdup(pdef.port);
			}
			break;

		case 'p': /* IP/Port of monitoring daemon */
			opts->http_port = optarg;
			break;

		case 'N': /* name to be registered for monitoring */
			strcpy(opts->monitor_name,optarg);
			break;

		case 'M': /* IP/Port of multicast monitoring daemon */
			process_cmdipopts(&opts->multicast,"224.10.10.20","20001");
			break;

		case 'U': /* IP/Port of unicast monitoring daemon */
			process_cmdipopts(&opts->unicast,"127.0.0.1","20001");
			break;

		case 'T': /* IP/Port of tcp monitoring daemon */
			process_cmdipopts(&opts->tcp,"127.0.0.1","20001");
			break;

		case 'L': /* Linger time */
			opts->linger = atoi(optarg);
			break;
		}
	}
	return 0;
}

/* Callback to process name lookup responses */
void process_name_responses(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc = stk_name_service_invoke(rcv_seq);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"invoke name service on sequence");
}

void name_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(httpd_dispatcher,flow,fd,NULL,process_name_responses);
	STK_ASSERT(STKA_HTTPD,added != -1,"add data flow to dispatcher fd %d df %p",fd,flow);
	printf("Name Server connected, fd %d\n",fd);

	if(rcv_dfs_registered) {
		stk_ret ret = stk_register_monitor_addresses(stk_env_from_data_flow(flow),rcv_df_ids[2],rcv_df_ids[1],rcv_df_ids[0]);
		STK_ASSERT(STKA_HTTPD,ret==STK_SUCCESS,"register monitoring addresses");
	}
}

void fd_destroyed_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int removed = dispatch_remove_fd(httpd_dispatcher,fd);
	STK_ASSERT(STKA_HTTPD,removed != -1,"Failed to remove data flow from dispatcher fd %d df %p",fd,flow);
	printf("Name Server disconnected, fd %d\n",fd);
}

void stkh_data_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	if (stk_get_data_flow_type(flow) == STK_TCP_ACCEPTED_FLOW) {
		int added = dispatch_add_accepted_fd(httpd_dispatcher,fd,flow,process_data);
		STK_ASSERT(STKA_HTTPD,added != -1,"add accepted data flow (fd %d) to dispatcher",fd);
	}
}

int stk_httpd_main(int shared,int argc,char *argv[])
{
	stk_env_t *stkbase;
	stk_ret rc;
	int defbufsize = 500;
	struct mg_context *ctx;
	const char *options[] = {"listening_ports", "8080", NULL};

	opts.linger = 300; /* Keep monitor name around for 5 mins after death */
	strcpy(opts.monitor_name,STK_HTTPD_DF_META_IDS);

	/* Get the command line options and fill out opts with user choices */
	if(process_cmdline(argc,argv,&opts) == -1) {
		stk_httpd_usage();
		exit(5);
	}

	if(opts.http_port)
		options[1] = opts.http_port;

	httpd_dispatcher = alloc_dispatcher();

	if(!shared) {
		signal(SIGTERM, stkhttpd_term); /* kill */
		signal(SIGINT, stkhttpd_term);  /* ctrl-c */
	}

	ctx = mg_start(&httpd_callback, NULL, options);
	STK_ASSERT(STKA_HTTPD,ctx != NULL,"start web server");

	{
	stk_ret ret;

	ret = stk_mutex_init(&service_list_lock);
	STK_ASSERT(STKA_HTTPD,ret==STK_SUCCESS,"create service history list lock");
	service_list = NewPList();
	STK_ASSERT(STKA_HTTPD,service_list != NULL,"create service list");

	ret = stk_mutex_init(&service_history_lock);
	STK_ASSERT(STKA_HTTPD,ret==STK_SUCCESS,"create service history list lock");
	service_history = NewPList();
	STK_ASSERT(STKA_HTTPD,service_history != NULL,"create service history list");
	}

	{
	stk_options_t name_server_data_flow_options[] = {  { "destination_address", "127.0.0.1"}, {"destination_port", "20002"}, { "nodelay", NULL},
		{ "fd_created_cb", (void *) name_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb },
		{ NULL, NULL } };
	stk_options_t name_server_options[] = { {"group_name", NULL},
		{ "name_server_data_flow_protocol", opts.name_server_protocol }, { "name_server_data_flow_options", name_server_data_flow_options },
		{ NULL, NULL } };

	if(opts.name_server_ip) name_server_data_flow_options[0].data = opts.name_server_ip;
	if(opts.name_server_port) name_server_data_flow_options[1].data = opts.name_server_port;
	if(opts.group_name[0] != '\0') name_server_options[0].data = opts.group_name;

	stkbase = stk_create_env(name_server_options);
	STK_ASSERT(STKA_HTTPD,stkbase!=NULL,"Failed to allocate an stk environment");
	}

	httpd_timers = stk_new_timer_set(stkbase,NULL,10,STK_TRUE);

	{
	stk_data_flow_t *tcp_df = NULL;
	stk_data_flow_t *udp_df = NULL;
	stk_data_flow_t *mcast_df = NULL;

	if(opts.multicast.ip)
	{
		stk_options_t udp_options[] = { { "bind_address", "0.0.0.0"}, {"bind_port", int_to_string_p(STK_HTTPD_RCV_DF_PORT)}, {"reuseaddr", (void*) STK_TRUE},
			{ "receive_buffer_size", "16000000" }, { "multicast_address", "224.10.10.20" }, { NULL, NULL } };
		int irc;

		/* Override defaults if configured */
		if(opts.multicast.ip) udp_options[4].data = opts.multicast.ip;
		if(opts.multicast.port) udp_options[1].data = opts.multicast.port;
		sprintf(rcv_df_ids[0],"udp:%s:%s",udp_options[4].data,udp_options[1].data);

		mcast_df = stk_udp_listener_create_data_flow(stkbase,"udp multicast listener socket for stkhttpd", STK_EG_SERVER_DATA_FLOW_ID, udp_options);
		STK_ASSERT(STKA_HTTPD,mcast_df!=NULL,"create multicast udp listener data flow");

		irc = dispatch_add_fd(httpd_dispatcher,mcast_df,stk_udp_listener_fd(mcast_df),NULL,process_data);
		STK_ASSERT(STKA_HTTPD,irc>=0,"Add multicast udp listener data flow to dispatcher");
	}

	if(opts.unicast.ip)
	{
		stk_options_t udp_options[] = { { "bind_address", "0.0.0.0"}, {"bind_port", int_to_string_p(STK_HTTPD_RCV_DF_PORT)}, {"reuseaddr", (void*) STK_TRUE},
			{ "receive_buffer_size", "16000000" }, { NULL, NULL } };
		int irc;

		/* Override defaults if configured */
		if(opts.unicast.ip) udp_options[0].data = opts.unicast.ip;
		if(opts.unicast.port) udp_options[1].data = opts.unicast.port;
		sprintf(rcv_df_ids[1],"udp:%s:%s",udp_options[0].data,udp_options[1].data);

		udp_df = stk_udp_listener_create_data_flow(stkbase,"udp listener socket for stkhttpd", STK_EG_SERVER_DATA_FLOW_ID, udp_options);
		STK_ASSERT(STKA_HTTPD,udp_df!=NULL,"create multicast udp listener data flow");

		irc = dispatch_add_fd(httpd_dispatcher,udp_df,stk_udp_listener_fd(udp_df),NULL,process_data);
		STK_ASSERT(STKA_HTTPD,irc>=0,"Add udp listener data flow to dispatcher");
	}

	{
	stk_options_t tcp_options[] = { { "bind_address", "0.0.0.0"}, {"bind_port", int_to_string_p(STK_HTTPD_RCV_DF_PORT)}, {"reuseaddr", (void*) STK_TRUE}, {"nodelay", (void*) STK_TRUE},
									{ "receive_buffer_size", "1024000" }, { "send_buffer_size", "256000" /* reduce memory usage */ },
									{ "fd_created_cb", (void *) stkh_data_fd_created_cb },
									{ NULL, NULL } };
	int added;

	/* Override defaults if configured */
	if(opts.tcp.ip) tcp_options[0].data = opts.tcp.ip;
	if(opts.tcp.port) tcp_options[1].data = opts.tcp.port;
	sprintf(rcv_df_ids[2],"tcp:%s:%s",tcp_options[0].data,tcp_options[1].data);

	tcp_df = stk_tcp_server_create_data_flow(stkbase,"tcp server socket for data flow test",STK_HTTPD_DATA_FLOW_ID,tcp_options);
	STK_ASSERT(STKA_HTTPD,tcp_df!=NULL,"create tcp server data flow");

	added = server_dispatch_add_fd(httpd_dispatcher,stk_tcp_server_fd(tcp_df),tcp_df,process_data);
	STK_ASSERT(STKA_HTTPD,added != -1,"Failed to add tcp server data flow to dispatcher");
	}

	rc = stk_register_monitor_addresses(stkbase,rcv_df_ids[2],rcv_df_ids[1],rcv_df_ids[0]);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"register monitoring addresses");
	rcv_dfs_registered = 1;

	/* Do this if its only multicast?? */
	registration_timer = stk_schedule_timer(httpd_timers,reregister_name_timer_cb,0,NULL,30000);

	update_svc_timer = stk_schedule_timer(httpd_timers,update_svc_timer_cb,0,NULL,800);

	while(stopped == 0)
		eg_dispatcher(httpd_dispatcher,stkbase,100);

	rc = stk_destroy_data_flow(tcp_df);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"destroy the tcp data flow: %d",rc);
	if(udp_df) {
		int removed = dispatch_remove_fd(httpd_dispatcher,stk_udp_listener_fd(udp_df));
		STK_ASSERT(STKA_HTTPD,removed != -1,"remove udp data flow from dispatcher");
		rc = stk_destroy_data_flow(udp_df);
		STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"destroy the udp data flow: %d",rc);
	}
	if(mcast_df) {
		int removed = dispatch_remove_fd(httpd_dispatcher,stk_udp_listener_fd(mcast_df));
		STK_ASSERT(STKA_HTTPD,removed != -1,"remove mcast data flow from dispatcher");
		rc = stk_destroy_data_flow(mcast_df);
		STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"destroy the mcast data flow: %d",rc);
	}
	}

	rc = stk_cancel_timer(httpd_timers,update_svc_timer);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"cancel the services timer : %d",rc);

	rc = stk_cancel_timer(httpd_timers,registration_timer);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"cancel the registration timer : %d",rc);

	rc = stk_free_timer_set(httpd_timers,STK_TRUE);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"destroy the timer set : %d",rc);

	while(!IsPListEmpty(service_list)) {
		Node *n = FirstNode(service_list);

		Remove(n);
		free_collected_service_data_node(n);
	}

	rc = stk_destroy_env(stkbase);
	STK_ASSERT(STKA_HTTPD,rc==STK_SUCCESS,"destroy a stk env object : %d",rc);

	mg_stop(ctx);

	free_dispatcher(httpd_dispatcher);

	return 0;
}

