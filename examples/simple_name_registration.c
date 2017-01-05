/*
 * Copyright Dave Trollope 2015
 *
 * This file implements a basic client for the name service
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <signal.h>
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_sequence_api.h"
#include "eg_dispatcher_api.h"
#include "stk_name_service.h"
#include "stk_name_service_api.h"
#include "stk_data_flow_api.h"
#ifdef __APPLE__
/* This should not be needed, but darwin still reports implicit declaration of strdup */
char *strdup(const char *);
#endif

/* Use examples header for asserts */
#include "stk_examples.h"

/* command line options provided - set in process_cmdline() */
struct cmdopts {
	char group_name[STK_MAX_GROUP_NAME_LEN];
	char name[STK_MAX_NAME_LEN];
	char *server_ip;
	char *server_port;
	char *server_protocol;
	char *name_server_ip;
	char *name_server_port;
	char name_server_protocol[5];
	short quiet;
	int cbs;
	int meta_data_id[100];
	char *meta_data_values[100];
	int meta_data_count;
	int linger;
	char *ft_state;
} opts;

/* Simple stat */
int cbs_rcvd;

void process_name_responses(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc = stk_name_service_invoke(rcv_seq);
	STK_ASSERT(rc==STK_SUCCESS,"invoke name service on sequence");
}

void usage()
{
	fprintf(stderr,"Usage: simple_name_registration [options] <name>\n");
	fprintf(stderr,"       -i [protocol:]<ip[:port]> : IP and port being registered\n");
	fprintf(stderr,"       -M <id,value>             : meta data (integer id, string value)\n");
	fprintf(stderr,"       -F <active|backup>        : Fault tolerant state to register with name\n");
	fprintf(stderr,"       -G <group name>           : Group Name that name should exist in\n");
	fprintf(stderr,"       -L <linger sec>           : Time name should exist after connection to name server dies\n");
	fprintf(stderr,"       -c #                      : Number of callbacks\n");
	fprintf(stderr,"       -R [protocol:]<ip[:port]> : IP and port of name server\n");
	fprintf(stderr,"                                 : protocol may be <tcp|udp>\n");
	fprintf(stderr,"       -q                        : quiet mode - no per message I/O\n");
	fprintf(stderr,"       -h                        : This help!\n");
}

/* Process command line options */
int process_cmdline(int argc,char *argv[],struct cmdopts *opts )
{
	int rc;

	while(1) {
		rc = getopt(argc, argv, "hqs:i:F:R:M:G:L:");
		if(rc == -1) break;

		switch(rc) {
		case 'c': /* Number of callbacks to expect for this registration */
			opts->cbs = atoi(optarg);
			break;

		case 'h': /* Help! */
			usage();
			exit(0);

		case 'F': /* Fault Tolerant State */
			opts->ft_state = optarg;
			break;

		case 'i': /* IP/Port of server to exchange data with */
			{
			stk_protocol_def_t pdef;
			stk_data_flow_parse_protocol_str(&pdef,optarg);

			if(pdef.protocol[0] != '\0') opts->server_protocol = strdup(pdef.protocol);
			if(pdef.ip[0] != '\0') opts->server_ip = strdup(pdef.ip);
			if(pdef.port[0] != '\0') opts->server_port = strdup(pdef.port);
			}
			break;

		case 'q': /* Be less verbose about whats happening */
			opts->quiet = 1;
			break;

		case 'L': /* Set the Linger time of this name */
			opts->linger = atoi(optarg);
			break;

		case 'R': /* Set the IP/Port of the Name Server */
			{
			stk_protocol_def_t pdef;
			stk_data_flow_parse_protocol_str(&pdef,optarg);

			if(pdef.protocol[0] != '\0') strcpy(opts->name_server_protocol,pdef.protocol);
			if(pdef.ip[0] != '\0') opts->name_server_ip = strdup(pdef.ip);
			if(pdef.port[0] != '\0') opts->name_server_port = strdup(pdef.port);
			}
			break;

		case 'M': /* Meta data to be registered with name */
			if(opts->meta_data_count < (int)(sizeof(opts->meta_data_values)/sizeof(opts->meta_data_values[0])))
			{
				char *comma;

				opts->meta_data_id[opts->meta_data_count] = atoi(optarg);
				comma = strchr(optarg,',');
				if(comma) {
					*comma = '\0';
					opts->meta_data_values[opts->meta_data_count] = ++comma;
				}

				opts->meta_data_count++;
			}
			break;

		case 'G': /* Name Server Group to register with */
			strncpy(opts->group_name,optarg,sizeof(opts->group_name));
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
	printf("Name Server connected, fd %d\n",fd);
}

void fd_destroyed_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int removed = dispatch_remove_fd(default_dispatcher(),fd);
	STK_ASSERT(removed != -1,"Failed to remove data flow from dispatcher");
	printf("Name Server disconnected, fd %d\n",fd);
}

void name_info_cb(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info,stk_name_info_cb_type cb_type)
{
	char ip[16];
	char *p;

	cbs_rcvd++;

	if(cb_type == STK_NS_REQUEST_EXPIRED) {
		if(!opts.quiet)
			printf("Request expired on name %s, %d callbacks received\n",name_info->name,cbs_rcvd);
		return;
	}

	p = (char *) inet_ntop(AF_INET, &name_info->ip[0].sockaddr.sin_addr, ip, sizeof(ip));
	if(p)
		printf("Received info on name %s, IP %s Port %d, FT State %d\n",
			name_info->name,ip,name_info->ip[0].sockaddr.sin_port,name_info->ft_state);
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
	opts.linger = 300; /* Keep names around for 5 mins after death - 5 mins by default */
	/* Set the default name server protocol */
	strcpy(opts.name_server_protocol,"tcp");

	/* Process command line options */
	if(process_cmdline(argc,argv,&opts) == -1) {
		usage();
		exit(5);
	}

	/* Only log errors to stderr when running quiet */
	if(opts.quiet)
		stk_set_stderr_level(STK_LOG_ERROR);

	/* Create the STK environment - can't do anything without one 
	 * NOTE: This example sets the group name for the entire environment,
	 * however, applications may pass the group_name option to either stk_create_env()
	 * or on a per registration basis when calling stk_register_name()
	 */
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
	STK_ASSERT(stkbase!=NULL,"Failed to allocate an stk environment");

	/* Process init time events, e.g. name server registration */
	client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
	}

	{
	/* This is registering 127.0.0.1:20003 as the IP/Port associated with this name - its not the server we are connecting to */
	stk_options_t name_options[] = { { "destination_address", "127.0.0.1"}, {"destination_port", "20003"}, { "destination_protocol", "tcp" }, { "fault_tolerant_state", "active"}, { NULL, NULL }, { NULL, NULL } };
	stk_sequence_t *meta_data_seq = NULL;

	if(opts.server_ip) name_options[0].data = opts.server_ip;
	if(opts.server_port) name_options[1].data = opts.server_port;
	if(opts.server_protocol) name_options[2].data = opts.server_protocol;
	if(opts.ft_state) name_options[3].data = opts.ft_state;

	if(opts.meta_data_count > 0) {
		meta_data_seq = stk_create_sequence(stkbase,"meta data sequence",0,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
		STK_ASSERT(meta_data_seq!=NULL,"Failed to allocate meta data sequence");

		for(int i = 0; i < opts.meta_data_count; i++) {
			if(opts.meta_data_values[i]) {
				rc = stk_copy_to_sequence(meta_data_seq, opts.meta_data_values[i], strlen(opts.meta_data_values[i]) + 1,
										  opts.meta_data_id[i]);
				STK_ASSERT(rc == STK_SUCCESS, "copy meta data %d", opts.meta_data_id[i]);
			}
		}

		/* Overwrite the NULL entry with the meta data sequence option */
		name_options[4].name = "meta_data_sequence";
		name_options[4].data = (void *) meta_data_seq;
	}

	printf ("Registering info on name %s\n",opts.name);

	rc = stk_register_name(stk_env_get_name_service(stkbase), opts.name, opts.linger, 1000, name_info_cb, NULL, name_options);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to register name");

	if(meta_data_seq) {
		rc = stk_destroy_sequence(meta_data_seq);
		STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the meta data sequence");
	}
	}

	/* Dispatch until we've received the expected number of callbacks */
	while(cbs_rcvd < opts.cbs) {
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
		printf("Received %d sequences, waiting for %d\n",cbs_rcvd,opts.cbs);
	}

	/* Free the dispatcher (and its related resources) */
	terminate_dispatcher(default_dispatcher());

	if(cbs_rcvd == 0) {
		puts("Draining for 5 seconds before closing");
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,5000);
	}

	/* And get rid of the environment, we are done! */
	rc = stk_destroy_env(stkbase);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	/* Output some basic info! */
	printf("%s: Received %d name registration callbacks\n",argv[0],cbs_rcvd);
	return 0;
}

