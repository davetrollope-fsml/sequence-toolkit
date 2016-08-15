/*
 * Copyright Dave Trollope 2015
 *
 * This file implements a basic client for the name service
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#ifdef __APPLE__
/* This should not be needed, but darwin still reports implicit declaration of strdup */
char *strdup(const char *);
#endif
#include <signal.h>
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_data_flow_api.h"
#include "stk_sequence_api.h"
#include "eg_dispatcher_api.h"
#include "stk_name_service.h"
#include "stk_name_service_api.h"

/* Use examples header for asserts */
#include "stk_examples.h"

/* command line options provided - set in process_cmdline() */
struct cmdopts {
	char group_name[STK_MAX_GROUP_NAME_LEN];
	char name[STK_MAX_NAME_LEN];
	int cbs;
	short subscribe;
	char *server_ip;
	char *server_port;
	char *name_server_ip;
	char *name_server_port;
	char name_server_protocol[5];
} opts;

/* Simple stat */
int cbs_rcvd;
int expired;

void process_name_responses(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc = stk_name_service_invoke(rcv_seq);
	STK_ASSERT(rc==STK_SUCCESS,"invoke name service on sequence");
}

void usage()
{
	fprintf(stderr,"Usage: simple_name_lookup [options] <name>\n");
	fprintf(stderr,"       -G <group name>           : Group Name that name should exist in\n");
	fprintf(stderr,"       -c #                      : Number of callbacks\n");
	fprintf(stderr,"       -h                        : This help!\n");
	fprintf(stderr,"       -R [protocol:]<ip[:port]> : IP and port of name server\n");
	fprintf(stderr,"                                 : protocol may be <tcp|udp>\n");
	fprintf(stderr,"       -X                        : Subscribe mode\n"); /* -S = service name in other examples */
}

/* Process command line options */
int process_cmdline(int argc,char *argv[],struct cmdopts *opts )
{
	int rc;

	while(1) {
		rc = getopt(argc, argv, "hXs:R:G:");
		if(rc == -1) break;

		switch(rc) {
		case 'c': /* Number of callbacks to expect for this registration */
			opts->cbs = atoi(optarg);
			break;

		case 'h': /* Help! */
			usage();
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

		case 'G': /* Name Server Group to register with */
			strncpy(opts->group_name,optarg,sizeof(opts->group_name));
			break;

		case 'X': /* Subscribe mode */
			opts->subscribe = 1;
			break;
		}
	}

	if(optind >= argc) return -1;
	strncpy(opts->name,argv[optind],sizeof(opts->name));

	return 0;
}

void name_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,NULL,process_name_responses);
	STK_ASSERT(added != -1,"Failed to add data flow to dispatcher");
	printf("Name Server connected\n");
}

void fd_destroyed_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int removed = dispatch_remove_fd(default_dispatcher(),fd);
	STK_ASSERT(removed != -1,"Failed to remove data flow from dispatcher");
	printf("Name Server disconnected\n");
}

stk_ret print_meta_data_cb(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	switch(user_type) {
	case STK_MD_HTTPD_TCP_ID:
		printf("HTTP monitoring TCP Connection %s\n",(char*) data);
		break;
	case STK_MD_HTTPD_UDP_ID:
		printf("HTTP monitoring UDP Connection %s\n",(char*) data);
		break;
	case STK_MD_HTTPD_MCAST_ID:
		printf("HTTP monitoring multicast Connection %s\n",(char*) data);
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
		cbs_rcvd++;
		expired = 1;
		printf("Request expired on name %s, %d callbacks received\n",name_info->name,cbs_rcvd);
		return;
	}

	p = (char *) inet_ntop(AF_INET, &name_info->ip[0].sockaddr.sin_addr, ip, sizeof(ip));
	if(p) 
		printf("Received info on name %s, IP %s Port %d Protocol %s, FT State %s\n",name_info->name,ip,
			name_info->ip[0].sockaddr.sin_port,name_info->ip[0].protocol,
			name_info->ft_state == STK_NAME_ACTIVE ? "active" : "backup");

	if(name_info->meta_data) {
		stk_ret rc;

		printf("Name %s has %d meta data elements\n",name_info->name,stk_number_of_sequence_elements(name_info->meta_data));

		rc = stk_iterate_sequence(name_info->meta_data,print_meta_data_cb,NULL);
		if(rc != STK_SUCCESS)
			printf("Error iterating over meta data %d\n",rc);
	}
	cbs_rcvd++;
}

int main(int argc,char *argv[])
{
	stk_env_t *stkbase;
	stk_sequence_t *seq;
	stk_sequence_t *ret_seq;
	stk_service_t *svc;
	stk_bool rc;

	sigignore(SIGPIPE); /* Some system benefit from ignoring SIGPIPE */

	opts.cbs = 2;

	strcpy(opts.name,"TEST");

	/* Process command line options */
	if(process_cmdline(argc,argv,&opts) == -1) {
		usage();
		exit(5);
	}

	/* Create the STK environment - can't do anything without one
	 * NOTE: This example sets the group name for the entire environment,
	 * however, applications may pass the group_name option to either stk_create_env()
	 * or on a per request basis when calling stk_request_name_info();
	 */

	{
	stk_options_t name_server_data_flow_options[] = { { "destination_address", "127.0.0.1"}, {"destination_port", "20002"}, { "nodelay", (void *)STK_TRUE},
		{ "fd_created_cb", (void *) name_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb }, { NULL, NULL } };
	stk_options_t name_server_options[] = { {"group_name", NULL},
		{ "name_server_data_flow_protocol", opts.name_server_protocol }, { "name_server_data_flow_options", name_server_data_flow_options },
		{ NULL, NULL } };

	if(opts.name_server_ip) name_server_data_flow_options[0].data = opts.name_server_ip;
	if(opts.name_server_port) name_server_data_flow_options[1].data = opts.name_server_port;
	if(opts.group_name[0] != '\0') name_server_options[0].data = opts.group_name;

	stkbase = stk_create_env(name_server_options);
	STK_ASSERT(stkbase!=NULL,"Failed to allocate an stk environment");

	/* Process init time events, e.g. name server registration */
	client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
	}

	{
	stk_ret ret;
	printf ("Requesting info on name %s\n",opts.name);

	if(opts.subscribe)
		ret = stk_subscribe_to_name_info(stk_env_get_name_service(stkbase), opts.name, name_info_cb, NULL, NULL);
	else
		ret = stk_request_name_info(stk_env_get_name_service(stkbase), opts.name, 1000, name_info_cb, NULL, NULL);
	STK_ASSERT(ret==STK_SUCCESS,"Failed to request name");
	}

	/* Dispatch until we've received the expected number of callbacks */
	while((opts.subscribe || (cbs_rcvd < opts.cbs)) && !expired) {
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
		if(opts.subscribe)
			printf("Received %d sequences\n",cbs_rcvd);
		else
			printf("Received %d sequences, waiting for %d\n",cbs_rcvd,opts.cbs);
	}

	/* Free the dispatcher (and its related resources) */
	terminate_dispatcher(default_dispatcher());

	/* And get rid of the environment, we are done! */
	rc = stk_destroy_env(stkbase);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	/* Output some basic info! */
	if(expired)
		printf("name lookup of '%s' expired\n",opts.name);
	else
		printf("Received %d name lookup callbacks for '%s'\n",cbs_rcvd,opts.name);
	return 0;
}

