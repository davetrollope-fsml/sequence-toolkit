/*
 * Copyright Dave Trollope 2013
 * This source code is not to be distributed without agreement from
 * D. Trollope
 *
 * This file implements a basic service for monitoring purposes only and is intended to be
 * a simple demonstration of how STK can be added to existing applications for monitoring.
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <strings.h>
#include <signal.h>
#ifdef __APPLE__
/* This should not be needed, but darwin still reports implicit declaration of strdup */
char *strdup(const char *);
#endif
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_sequence_api.h"
#include "stk_tcp_client_api.h"
#include "stk_service_group_api.h"
#include "stk_sg_automation_api.h"
#include "stk_tcp_server_api.h"
#include "stk_data_flow_api.h"
#include "stk_tcp.h"
#include "stk_ids.h"
#include "stk_name_service_api.h"
#include "eg_dispatcher_api.h"

/* Use examples header for asserts */
#include "stk_examples.h"

/* command line options provided - set in process_cmdline() */
struct cmdopts {
	char service_name[128];
	unsigned long end_threshold;
	STK_NAME_SERVER_OPTS
	STK_MONITOR_OPTS
} opts;

int name_lookup_expired;
int name_lookup_cbs_rcvd;

/* Process command line options */
int process_cmdline(int argc,char *argv[],struct cmdopts *opts )
{
	int rc;

	while(1) {
		rc = getopt(argc, argv, "m:t:S:");
		if(rc == -1) return 0;

		switch(rc) {
		case 'm': /* IP/Port of monitoring daemon */
			process_monitoring_string(opts,optarg);
			break;

		case 'R': /* Set the IP/Port of the Name Server */
			process_name_server_string(opts,optarg);
			break;

		case 't': /* Checkpoint to reach to force exit */
			opts->end_threshold = atol(optarg);;
			break;

		case 'S': /* The service name to represent this application */
			strncpy(opts->service_name,optarg,sizeof(opts->service_name));
			break;
		}
	}
	return 0;
}

stk_ret print_meta_data_cb(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	switch(user_type) {
	case STK_MD_HTTPD_TCP_ID:
		printf("HTTP monitoring TCP Connection %s\n",(char*) data);
		process_monitoring_string(&opts,(char *) data);
		break;
	case STK_MD_HTTPD_UDP_ID:
		printf("HTTP monitoring UDP Connection %s\n",(char*) data);
		process_monitoring_string(&opts,(char *) data);
		break;
	case STK_MD_HTTPD_MCAST_ID:
		printf("HTTP monitoring multicast Connection %s\n",(char*) data);
		process_monitoring_string(&opts,(char *) data);
		break;
	default:
		printf("Meta data type %lu sz %lu\n",user_type,sz);
	}
	return STK_SUCCESS;
}

void name_info_cb(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info,stk_name_info_cb_type cb_type)
{
	char ip[16];
	char *p;

	if(cb_type == STK_NS_REQUEST_EXPIRED) {
		name_lookup_cbs_rcvd++;
		name_lookup_expired = 1;
		printf("Request expired on name %s, %d callbacks received\n",name_info->name,name_lookup_cbs_rcvd);
		return;
	}

	printf("Received info on name %s, IP %s Port %d Protocol %s\n",name_info->name,name_info->ip[0].ipstr,
		name_info->ip[0].sockaddr.sin_port,name_info->ip[0].protocol[0] ? name_info->ip[0].protocol : "unknown");

	if(opts.monitor_name && strcmp(name_info->name,opts.monitor_name) == 0) {
		opts.monitor_ip = strdup(name_info->ip[0].ipstr);
		opts.monitor_port = strdup(name_info->ip[0].portstr);
	}

	if(name_info->meta_data) {
		stk_ret rc;

		printf("Name %s has %d meta data elements\n",name_info->name,stk_number_of_sequence_elements(name_info->meta_data));

		rc = stk_iterate_sequence(name_info->meta_data,print_meta_data_cb,NULL);
		if(rc != STK_SUCCESS)
			printf("Error iterating over meta data %d\n",rc);
	}
	name_lookup_cbs_rcvd++;
}

void fd_hangup_cb(stk_dispatcher_t *d,stk_data_flow_t *flow,int fd)
{
	int removed = dispatch_remove_fd(d,fd);
	STK_ASSERT(removed != -1,"remove data flow from dispatcher");

	/* Force closing of fd on data flow */
	{
	stk_ret ret = stk_tcp_client_unhook_data_flow(flow);
	STK_ASSERT(ret==STK_SUCCESS,"unhook fd from data flow");
	}
}

/* Callback to process name lookup responses */
void process_name_responses(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc = stk_name_service_invoke(rcv_seq);
	STK_ASSERT(rc==STK_SUCCESS,"invoke name service on sequence");
}

void name_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,fd_hangup_cb,process_name_responses);
	STK_ASSERT(added != -1,"Failed to add data flow to dispatcher");
}

void monitoring_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	printf("Monitoring channel created\n");
}

void fd_destroyed_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	printf("Monitoring channel destroyed\n");
}

int main(int argc,char *argv[])
{
	stk_env_t *stkbase;
	stk_sequence_t *seq;
	stk_sequence_t *ret_seq;
	stk_service_t *svc;
	stk_data_flow_t *df,*monitoring_df;
	stk_bool rc;
	stk_sequence_id snd_id,rcv_id;

	sigignore(SIGPIPE); /* Some system benefit from ignoring SIGPIPE */

	/* Set the default service name */
	strcpy(opts.service_name,"basic monitored service");
	opts.end_threshold = 1000000;

	/* Process command line options */
	if(process_cmdline(argc,argv,&opts) == -1) {
		fprintf(stderr,"Usage: monitored_service [options]");
		fprintf(stderr,"       -t <threshold>            : End threshold (iterations)");
		fprintf(stderr,"       -S <name>                 : Service Name");
		fprintf(stderr,"       -m lookup:<name>          : Lookup <name> to get the protocol/ip/port from the name server\n");
		fprintf(stderr,"       or <[protocol:]ip[:port]> : IP and port of monitor (default: tcp:127.0.0.1:20001)\n");
		fprintf(stderr,"       -R <[protocol:]ip[:port]> : IP and port of name server\n");
		fprintf(stderr,"                                 : protocol may be <tcp|udp>\n");
		exit(5);
	}

	/* Create the STK environment - can't do anything without one. Configure the
	 * address/port of the name server and monitoring web server. Both are optional,
	 * and the monitoring configuration may be passed to service creation but for
	 * convenience we configure it in the env so it can be shared. Connections
	 * will be established to the servers. See simple_server.c for an example
	 * of creating the env without configuring these.
	 */
	{
	stk_options_t name_server_data_flow_options[] = {
		{ "destination_address", "127.0.0.1"}, {"destination_port", "20002"}, { "nodelay", (void *)STK_TRUE},
		{ "data_flow_name", "name server data flow for monitored_service"}, { "data_flow_id", (void *) STK_NAME_SERVICE_DATA_FLOW_ID },
		{ "fd_created_cb", (void *) name_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb }, { NULL, NULL } };
	stk_options_t name_server_options[] = { {"group_name", NULL},
		{ "name_server_data_flow_protocol", opts.name_server_protocol }, { "name_server_data_flow_options", name_server_data_flow_options },
		{ NULL, NULL } };
	stk_options_t env_options[] = { { "name_server_options", name_server_options }, { NULL, NULL } };

	if(opts.name_server_ip) name_server_data_flow_options[0].data = opts.name_server_ip;
	if(opts.name_server_port) name_server_data_flow_options[1].data = opts.name_server_port;

	stkbase = stk_create_env(env_options);
	STK_ASSERT(stkbase!=NULL,"Failed to allocate an stk environment");

	/* Process init time events, e.g. name server registration */
	client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
	}

	/* Now the env is created, use it to create the monitoring connection.
	 * Use the name server to locate the Protocol/IP/Port if needed
	 */
	{
	stk_options_t monitoring_data_flow_options[] = {
		{ "data_flow_name", "monitoring socket for monitored_service"}, { "data_flow_id", (void *) STK_HTTPD_DATA_FLOW_ID },
		{ "destination_address", "127.0.0.1"}, {"destination_port", "20001"}, { "nodelay", (void*) STK_TRUE},
		{ "fd_created_cb", (void *) monitoring_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb }, { NULL, NULL }
		};
	stk_options_t monitoring_options[] = { 
		{ "monitoring_data_flow_protocol", opts.monitor_protocol }, { "monitoring_data_flow_options", &monitoring_data_flow_options }, 
		{ NULL, NULL } };

	if(strcasecmp(opts.monitor_protocol,"lookup") == 0)
		name_lookup_and_dispatch(stkbase, opts.monitor_name, name_info_cb, name_lookup_expired, name_lookup_cbs_rcvd, 0);

	if(opts.monitor_ip) monitoring_data_flow_options[2].data = opts.monitor_ip;
	if(opts.monitor_port) monitoring_data_flow_options[3].data = opts.monitor_port;

	rc = stk_set_env_monitoring_data_flow(stkbase,monitoring_options);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to set environment monitoring data flow");
	}

	/* Create our simple client service, monitoring is inherited from the environment */
	{
	stk_service_id svc_id;
	struct timeval tv;

	gettimeofday(&tv,NULL);
	srand((unsigned int) (tv.tv_sec + tv.tv_usec)); 
	svc_id = rand();

	printf("Using random service ID: %lu\n",svc_id);

	svc = stk_create_service(stkbase,opts.service_name, svc_id, STK_SERVICE_TYPE_DATA, NULL);
	STK_ASSERT(svc!=NULL,"Failed to create a basic named data service object");
	}

	/* Set this service to a running state so folks know we are in good shape */
	rc = stk_set_service_state(svc,STK_SERVICE_STATE_RUNNING);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to set service state of RUNNING : %d",rc);

	/* Start the application logic updating the checkpoint of the service and
	 * invoke the timer handler to allow heartbeats to be sent
	 */
	{
	stk_checkpoint_t checkpoint = 0;
	volatile long x = 1;

	while(1)
	{
		/* Do some application logic */
		x = x * 2;
		printf("The new value of x is %lu\n",x);

		/* Update this service's checkpoint so others know we are doing sane things */
		stk_service_update_smartbeat_checkpoint(svc,(stk_checkpoint_t) checkpoint++);

		rc = stk_env_dispatch_timer_pools(stkbase,0);
		STK_ASSERT(rc==STK_SUCCESS,"Failed to dispatch timers : %d",rc);

		/* See if its time to end */
		if(opts.end_threshold > 0 && checkpoint > opts.end_threshold) break;

		sleep(1);
	}
	}

	/* Set this service to state 'stopping' so folks know we are in ending */
	rc = stk_set_service_state(svc,STK_SERVICE_STATE_STOPPING);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to set service state of STOPPING : %d",rc);

	/* Ok, now we can get rid of the service */
	rc = stk_destroy_service(svc,NULL);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the service : %d",rc);

	/* And get rid of the environment, we are done! */
	rc = stk_destroy_env(stkbase);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	/* Output some basic info! */
	printf("%s hit its threshold of %lu\n",opts.service_name,opts.end_threshold);
	return 0;
}

