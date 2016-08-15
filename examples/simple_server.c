/*
 * Copyright Dave Trollope 2015
 *
 * This example demonstrates the use of a service group to receive
 * state updates about services and to receive/send data on data flows.
 *
 * It creates a service group, registers callbacks for state changes,
 * smartbeats and reflects received sequences back to the sender.
 *
 * It supports TCP and UDP (Raw, Unicast and Multicast) data flows.
 */
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_sequence_api.h"
#include "stk_data_flow.h"
#include "stk_tcp_server_api.h"
#include "stk_tcp_client_api.h"
#include "stk_udp_client_api.h"
#include "stk_udp_listener_api.h"
#include "stk_data_flow_api.h"
#include "stk_service_group_api.h"
#include "stk_sg_automation_api.h"
#include "stk_name_service_api.h"
#include "stk_tcp.h"
#include "stk_ids.h"
#include "stk_examples.h"
#include "eg_dispatcher_api.h"

#include <stdio.h>
#include <getopt.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <poll.h>
#include <signal.h>
#ifdef __APPLE__
/* This should not be needed, but darwin still reports implicit declaration of strdup */
char *strdup(const char *);
#endif
 
void term(int signum)
{
	static int ending = 0;

	if(ending++) exit(5);

	printf("Received SIGTERM/SIGINT, exiting...\n");
	stop_dispatching(default_dispatcher());
}
 

/* This global stores the service group handle used by this example
 * As clients are detected, and added and removed to/from the service group,
 * this is the handle used to reference that service group
 */
stk_service_group_t *svcgrp;

/* command line options provided - set in process_cmdline() */
struct cmdopts {
	char group_name[128];
	short quiet;
	short passive;
	char *multicast_ip;
	char *multicast_port;
	char protocol;
	/* See stk_examples.h */
	STK_NAME_SERVER_OPTS
	STK_MONITOR_OPTS
	STK_BIND_OPTS
} opts;

/* Stats */
int seqs_rcvd,failed_sends;
int name_lookup_expired;
int name_lookup_cbs_rcvd;

stk_ret process_seq_segment(stk_sequence_t *seq, void *vdata, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	unsigned char *data = (unsigned char *) vdata;

	if(!opts.quiet) {
		printf("Sequence %p Received %ld bytes of type %ld\n",seq,sz,user_type);

		if(data && sz >= 4) {
			printf("Bytes: %02x %02x %02x %02x ... %02x %02x %02x %02x\n",
				data[0],data[1],data[2],data[3],data[sz - 4],data[sz - 3],data[sz - 2],data[sz - 1]);
		}
	}

	return STK_SUCCESS;
}

/* Callback to process received sequence data */
void process_data(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc;

	if(stk_get_sequence_type(rcv_seq) != STK_SEQUENCE_TYPE_DATA) {
		rc = stk_sga_invoke(svcgrp,rcv_seq);
		STK_ASSERT(rc==STK_SUCCESS,"Invocation of sequence %p to create source on service group %p failed %d",rcv_seq,svcgrp,rc);
	}

	if(!opts.quiet) {
		stk_sequence_type t = stk_get_sequence_type(rcv_seq);
		printf("data flow %p: Number of elements in received sequence: %d Sequence type: %s\n",rcvchannel,stk_number_of_sequence_elements(rcv_seq),STK_SEQ_TYPE_TO_STRING(t));
	}
	if(stk_get_sequence_type(rcv_seq) != STK_SEQUENCE_TYPE_DATA) return;

	/* Call process_seq_segment() on each element in the sequence */
	rc = stk_iterate_sequence(rcv_seq,process_seq_segment,NULL);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to process received sequence");

	if(!opts.passive) {
		stk_env_t *stkbase = stk_env_from_sequence(rcv_seq);
		stk_sequence_t *ret_seq = stk_create_sequence(stkbase,"simple_server_return_data",0x7edcba90,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
		char retbuf[10];

		for(unsigned int i = 0; i < sizeof(retbuf); i++)
			retbuf[i] = i;

		{
		static short num;
		rc = stk_copy_to_sequence(ret_seq,retbuf,sizeof(retbuf),num++);
		STK_ASSERT(rc==STK_SUCCESS,"Failed to copy return sequence number to sequence %p",ret_seq);
		}

		{ /* Transfer the originating IP to the returning sequence */
		struct sockaddr_in client_ip;
		socklen_t addrlen;
		rc = stk_data_flow_client_ip(rcv_seq,&client_ip,&addrlen);
		if(rc == STK_SUCCESS) {
			rc = stk_data_flow_add_client_ip(ret_seq,&client_ip,addrlen);
			STK_ASSERT(rc==STK_SUCCESS,"add client ip while processing request");
		}
		}

		rc = stk_data_flow_send(rcvchannel,ret_seq,STK_TCP_SEND_FLAG_NONBLOCK);
		if(rc != STK_SUCCESS) {
			failed_sends++;
			if(!opts.quiet)
				STK_LOG(STK_LOG_NORMAL,"Failed to send return data to client. Failed sends: %d",failed_sends);
		}

		rc = stk_destroy_sequence(ret_seq);
		STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy return sequence %p",ret_seq);

		seqs_rcvd++;
		if(!opts.quiet)
			printf("Received: %d Failed sends: %d\n",seqs_rcvd,failed_sends);
	}
}

stk_ret print_meta_data_cb(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	switch(user_type) {
	case STK_MD_HTTPD_TCP_ID:
		if(!opts.quiet)
			printf("HTTP monitoring TCP Connection %s\n",(char*) data);
		process_monitoring_string(&opts,(char *) data);
		break;
	case STK_MD_HTTPD_UDP_ID:
		if(!opts.quiet)
			printf("HTTP monitoring UDP Connection %s\n",(char*) data);
		process_monitoring_string(&opts,(char *) data);
		break;
	case STK_MD_HTTPD_MCAST_ID:
		if(!opts.quiet)
			printf("HTTP monitoring multicast Connection %s\n",(char*) data);
		process_monitoring_string(&opts,(char *) data);
		break;
	default:
		if(!opts.quiet)
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
		if(!opts.quiet)
			printf("Request expired on name %s, %d callbacks received\n",name_info->name,name_lookup_cbs_rcvd);
		return;
	}

	p = (char *) inet_ntop(AF_INET, &name_info->ip[0].sockaddr.sin_addr, ip, sizeof(ip));
	if(p) {
		char monitor_port[6];
		if(!opts.quiet)
			printf("Received info on name %s, IP %s Port %d\n",name_info->name,ip,name_info->ip[0].sockaddr.sin_port);
 		sprintf(monitor_port,"%d",name_info->ip[0].sockaddr.sin_port);
		opts.monitor_ip = strdup(ip);
		opts.monitor_port = strdup(monitor_port);
	}

	if(name_info->meta_data) {
		stk_ret rc;

		if(!opts.quiet)
			printf("Name %s has %d meta data elements\n",name_info->name,stk_number_of_sequence_elements(name_info->meta_data));

		rc = stk_iterate_sequence(name_info->meta_data,print_meta_data_cb,NULL);
		if(rc != STK_SUCCESS)
			printf("Error iterating over meta data %d\n",rc);
	}
	name_lookup_cbs_rcvd++;
}

/* Callback to process name lookup responses */
void process_name_responses(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc = stk_name_service_invoke(rcv_seq);
	STK_ASSERT(rc==STK_SUCCESS,"invoke name service on sequence");
}

void service_state_change_cb(stk_service_t *svc,stk_service_state old_state,stk_service_state new_state)
{
	char old_state_str[STK_SERVICE_STATE_NAME_MAX];
	char new_state_str[STK_SERVICE_STATE_NAME_MAX];

	stk_get_service_state_str(svc,old_state,old_state_str,STK_SERVICE_STATE_NAME_MAX);
	stk_get_service_state_str(svc,new_state,new_state_str,STK_SERVICE_STATE_NAME_MAX);
	printf("Service '%s' changed from state %s to %s\n",stk_get_service_name(svc),old_state_str,new_state_str);
}

void service_added_cb(stk_service_group_t *svc_group, stk_service_t *svc,stk_service_in_group_state state)
{
	printf("Service '%s' added to service group '%s' [state %d]\n",stk_get_service_name(svc),stk_get_service_group_name(svc_group),state);
}

void service_removed_cb(stk_service_group_t *svc_group, stk_service_t *svc,stk_service_in_group_state state)
{
	printf("Service '%s' removed from service group '%s' [state %d]\n",stk_get_service_name(svc),stk_get_service_group_name(svc_group),state);
	printf("Received: %d Failed sends: %d\n",seqs_rcvd,failed_sends);
}

void service_smartbeat_cb(stk_service_group_t *svc_group, stk_service_t *svc,stk_smartbeat_t *smartbeat)
{
	printf("Service '%s' group '%s' smartbeat received, checkpoint %ld.\n",stk_get_service_name(svc),stk_get_service_group_name(svc_group),smartbeat->checkpoint);
}

void usage()
{
	fprintf(stderr,"Usage: simple_server [options]\n");
	fprintf(stderr,"       -h                             : This help!\n");
	fprintf(stderr,"       -q                             : Quiet\n");
	fprintf(stderr,"       -0                             : 0 Responses (passive mode)\n");
	fprintf(stderr,"       -B ip[:port]                   : IP and port to be bound (default: 0.0.0.0:29312)\n");
	fprintf(stderr,"       -G <name>                      : Group Name for services\n");
	fprintf(stderr,"       -m lookup:<name>               : Lookup <name> to get the protocol/ip/port from the name server\n");
	fprintf(stderr,"       or <[protocol:]ip[:port]>      : IP and port of monitor (default: tcp:127.0.0.1:20001)\n");
	fprintf(stderr,"                                      : protocol may be <tcp|udp>\n");
	fprintf(stderr,"       -M <ip[:port]>                 : Multicast Listening IP and port (default 224.10.10.20)\n");
	fprintf(stderr,"       -P <tcp|udp|multicast>         : Protocol to receive on [default tcp]\n");
	fprintf(stderr,"       -R <[protocol:]ip[:port]>      : IP and port of name server\n");
	fprintf(stderr,"                                      : protocol may be <tcp|udp>\n");
}

int process_cmdline(int argc,char *argv[],struct cmdopts *opts )
{
	int rc;

	while(1) {
		rc = getopt(argc, argv, "0hqG:B:P:m:M:R:");
		if(rc == -1) return 0;

		switch(rc) {
		case 'h': /* Help! */
			usage();
			exit(0);

		case 'G': /* Set the Service Group Name (this is not the name server group) */
			strncpy(opts->group_name,optarg,sizeof(opts->group_name));
			break;

		case 'm': /* IP/Port of monitoring daemon */
			process_monitoring_string(opts,optarg);
			break;

		case 'R': /* Set the IP/Port of the Name Server */
			process_name_server_string(opts,optarg);
			break;

		case 'B': /* Set the IP/Port to bind to */
			process_bind_string(opts,optarg);
			break;

		case 'M': /* Multicast IP/Port to listen on */
			{
			char *colon;

			opts->multicast_ip = optarg;
			colon = strchr(optarg,':');
			if(colon) {
				*colon = '\0';
				opts->multicast_port = ++colon;
			}
			opts->protocol = 2; /* Force to multicast protocol */
			}
			break;

		case 'P': /* Set the protocol */
			if(strcasecmp(optarg,"udp") == 0) opts->protocol = 1;
			if(strcasecmp(optarg,"multicast") == 0) opts->protocol = 2;
			break;

		case 'q': /* Be less verbose about whats happening */
			opts->quiet = 1;
			break;

		case '0': /* Don't respond to data */
			opts->passive = 1;
			break;
		}
	}
	return 0;
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

void fd_destroyed_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int removed = dispatch_remove_fd(default_dispatcher(),fd);
	STK_ASSERT(removed != -1,"Failed to remove data flow (fd %d) from dispatcher",fd);
}

void name_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,fd_hangup_cb,process_name_responses);
	STK_ASSERT(added != -1,"Failed to add data flow (fd %d) to dispatcher",fd);
}

void data_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	if (stk_get_data_flow_type(flow) == STK_TCP_SERVER_FLOW) {
		int added = server_dispatch_add_fd(default_dispatcher(),fd,flow,process_data);
		STK_ASSERT(added != -1,"add server data flow (fd %d) to dispatcher",fd);
	} else if (stk_get_data_flow_type(flow) == STK_TCP_ACCEPTED_FLOW) {
		int added = dispatch_add_accepted_fd(default_dispatcher(),fd,flow,process_data);
		STK_ASSERT(added != -1,"add accepted data flow (fd %d) to dispatcher",fd);
	} else {
		int added = dispatch_add_fd(default_dispatcher(),flow,fd,fd_hangup_cb,process_data);
		STK_ASSERT(added != -1,"add data flow (fd %d) to dispatcher",fd);
	}
}

int main(int argc,char *argv[])
{
	stk_env_t *stkbase;
	stk_bool rc;
	int defbufsize = 500;

	signal(SIGTERM, term); /* kill */
	signal(SIGINT, term);  /* ctrl-c */
	sigignore(SIGPIPE); /* Some system benefit from ignoring SIGPIPE */

	/* Set the default service group name which maybe
	 * changed as part of commandline prpcessing.
	 */
	strcpy(opts.group_name,"Simple Server Service Group");
	/* Set the default monitoring protocol */
	strcpy(opts.monitor_protocol,"tcp");

	/* Get the command line options and fill out opts with user choices */
	if(process_cmdline(argc,argv,&opts) == -1) {
		usage();
		exit(5);
	}

	/* Only log errors to stderr when running quiet */
	if(opts.quiet)
		stk_set_stderr_level(STK_LOG_ERROR);

	if(!opts.bind_ip && opts.protocol != 2)
		printf("WARNING: No bind IP specified, can't register service group name\n");

	/*
	 * Create an STK environment. Since we are using the example listening dispatcher,
	 * set an option for the environment to ensure the dispatcher wakeup API is called.
	 */
	{
	stk_options_t name_server_data_flow_options[] = { { "destination_address", "127.0.0.1"}, {"destination_port", "20002"}, { "nodelay", (void *)STK_TRUE},
		{ "data_flow_name", "name server data flow for simple_server"}, { "data_flow_id", (void *) STK_NAME_SERVICE_DATA_FLOW_ID },
		{ "fd_created_cb", (void *) name_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb }, { NULL, NULL } };
	stk_options_t name_server_options[] = { {"group_name", NULL},
		{ "name_server_data_flow_protocol", opts.name_server_protocol }, { "name_server_data_flow_options", name_server_data_flow_options },
		{ NULL, NULL } };
	stk_options_t env_opts[] = { { "name_server_options", name_server_options },
		{ "wakeup_cb", (void *) wakeup_dispatcher}, { NULL, NULL } };

	if(opts.name_server_ip) name_server_data_flow_options[0].data = opts.name_server_ip;
	if(opts.name_server_port) name_server_data_flow_options[1].data = opts.name_server_port;

	stkbase = stk_create_env(env_opts);
	STK_ASSERT(stkbase!=NULL,"Failed to allocate an stk environment");

	/* Process init time events, e.g. name server registration */
	client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
	}

	{
	stk_data_flow_t *df;
	stk_data_flow_t *monitoring_df;
	stk_options_t svcgrp_opts[7] = { { "service_added_cb", (void *) service_added_cb }, { "service_removed_cb", (void *) service_removed_cb }, 
									 { "state_change_cb", (void *) service_state_change_cb }, { "service_smartbeat_cb", (void *) service_smartbeat_cb },
									 { "monitoring_data_flow", NULL }, { "listening_data_flow", NULL }, { NULL, NULL } };

	/* Create a data flow exclusively for monitoring */
	{
	stk_options_t data_flow_options[] = {
		{ "destination_address", "127.0.0.1"}, {"destination_port", "20001"},
		{ "nodelay", (void*) STK_TRUE}, { NULL, NULL } };

	if(strcasecmp(opts.monitor_protocol,"lookup") == 0)
		name_lookup_and_dispatch(stkbase, opts.monitor_name, name_info_cb, name_lookup_expired, name_lookup_cbs_rcvd,0);

	if(opts.monitor_ip) data_flow_options[0].data = opts.monitor_ip;
	if(opts.monitor_port) data_flow_options[1].data = opts.monitor_port;

	if(!strcasecmp(opts.monitor_protocol,"udp"))
		monitoring_df = stk_udp_client_create_data_flow(stkbase,"udp monitoring socket for simple_server", STK_HTTPD_DATA_FLOW_ID, data_flow_options);
	else
		monitoring_df = stk_tcp_client_create_data_flow(stkbase,"tcp monitoring socket for simple_server", STK_HTTPD_DATA_FLOW_ID, data_flow_options);
	STK_ASSERT(monitoring_df!=NULL,"Failed to create monitoring data flow");

	svcgrp_opts[4].data = monitoring_df;
	}

	/*
	 * Create the server data flow (aka a listening socket)
	 */
	switch(opts.protocol)
	{
	case 2:
	case 1:
		{
		stk_options_t udp_options[] = { { "bind_address", "0.0.0.0"}, {"bind_port", "29312"}, { "reuseaddr", (void *) STK_TRUE },
			{ "receive_buffer_size", "16000000" },
			{ "fd_created_cb", (void *) data_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb },
			{ NULL, NULL /* placeholder for multicast */ },
			{ NULL, NULL } };

		if(opts.protocol == 2) {
			/* Set default multicast options */
			udp_options[6].name = "multicast_address";
			udp_options[6].data = "224.10.10.20";

			/* Override defaults if configured */
			if(opts.multicast_ip) udp_options[6].data = opts.multicast_ip;
			if(opts.multicast_port) udp_options[1].data = opts.multicast_port;
		}

		if(opts.bind_ip)
			udp_options[0].data = opts.bind_ip;
		if(opts.bind_port)
			udp_options[1].data = opts.bind_port;

		df = stk_udp_listener_create_data_flow(stkbase,"udp listener socket for simple_server", STK_EG_SERVER_DATA_FLOW_ID, udp_options);
		STK_ASSERT(df!=NULL,"Failed to create udp listener data flow");
		break;
		}

	default:
		{
		stk_options_t tcp_options[] = { { "bind_address", "0.0.0.0"}, {"bind_port", "29312"}, {"nodelay", NULL},
			{ "send_buffer_size", "800000" }, { "receive_buffer_size", "16000000" },{ "reuseaddr", (void *) 1 },
			{ "fd_created_cb", (void *) data_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb },
			{ NULL, NULL } };

		if(opts.bind_ip)
			tcp_options[0].data = opts.bind_ip;
		if(opts.bind_port)
			tcp_options[1].data = opts.bind_port;

		df = stk_tcp_server_create_data_flow(stkbase,"tcp server socket for simple_server", STK_EG_SERVER_DATA_FLOW_ID, tcp_options);
		STK_ASSERT(df!=NULL,"Failed to create tcp server data flow");
		break;
		}
	}

	/* Set the listening data flow for the service group so the group name can be registered with the data flow IP */
	svcgrp_opts[5].data = df;

	/*
	 * Create the service group that client services will be added to as they are discovered.
	 * Also, register callbacks so we can be notified when services are added and removed.
	 */
	printf("Creating service group '%s'\n", opts.group_name);
	svcgrp = stk_create_service_group(stkbase, opts.group_name, 1000, svcgrp_opts);
	STK_ASSERT(svcgrp!=NULL,"Failed to create the service group");

	/*
	 * Run the example listening dispatcher to accept data flows from clients
	 * and receive data from them. This example does this inline, but an
	 * application might choose to invoke this on another thread.
	 * 
	 * The dispatcher only returns when a shutdown is detected.
	 */
	eg_dispatcher(default_dispatcher(),stkbase,100);

	terminate_dispatcher(default_dispatcher());

	/* The dispatcher returned, destroy the data flow, sequence, service group and environment */
	rc = stk_destroy_data_flow(df);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the tcp data flow: %d",rc);

	rc = stk_destroy_service_group(svcgrp);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the service group object : %d",rc);

	rc = stk_destroy_data_flow(monitoring_df);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the monitoring data flow : %d",rc);
	}

	rc = stk_destroy_env(stkbase);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	printf("%s terminated\n",argv[0]);
	return 0;
}

