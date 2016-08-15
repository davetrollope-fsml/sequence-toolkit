/*
 * This file implements a basic type of bridge that receives raw multicast using the
 * raw UDP client and sends each packet on a TCP data flow.
 *
 * Example Usage:
 *   ./raw_multicast_bridge 224.10.10.50:25000 127.0.0.1:29312
 *
 * Each raw multicast UDP packet received is put in to a sequence with one 
 * element by the raw UDP module so its easy to manage processing it.
 *
 * It creates a service for monitoring purposes and updates the checkpoint
 * once a second so its clear that the dispatching thread is still running
 *
 * This example can also be used for regular unicast UDP by removing the multicast
 * configuration - but the use cases requiring this are less common.
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
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
#include "stk_rawudp_api.h"
#include "stk_data_flow_api.h"
#include "stk_name_service_api.h"
#include "stk_tcp.h"
#include "stk_udp.h"
#include "stk_ids.h"
#include "eg_dispatcher_api.h"

int ending = 0;
void term(int signum)
{
	if(ending++) exit(5);

	printf("Received SIGTERM/SIGINT, exiting...\n");
	stop_dispatching(default_dispatcher());
}
 
/* This example batches up sequences to be processed for efficiency */
#define BATCH_SIZE 5

/* Use examples header for asserts */
#include "stk_examples.h"

/* command line options provided - set in process_cmdline() */
struct cmdopts {
	char service_name[128];
	int seqs;
	short quiet;
	char *server_ip;
	char *server_port;
	char *mcast_recv_addr;
	char *mcast_recv_port;
	STK_NAME_SERVER_OPTS
	STK_MONITOR_OPTS
} opts;

/* Simple stats */
int seqs_rcvd,blks,drops,seqs_sent;
/* TCP outgoing data flow */
stk_data_flow_t *df;
/* Name server querying flags */
int name_lookup_expired;
int name_lookup_cbs_rcvd;

/* Callback to process received sequence data */
void process_data(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	/* Iterate over each segment and call process_seq_segment() for each */
	stk_ret rc;

	if(rcvchannel == df) return; /* ignore data coming back from tcp */

	/* Count the sequence as received */
	seqs_rcvd++;

	/* Send the sequence! */
	rc = stk_data_flow_send(df,rcv_seq,STK_TCP_SEND_FLAG_NONBLOCK);
	if(rc == STK_SUCCESS || rc == STK_WOULDBLOCK) {
		if(rc == STK_SUCCESS)
			seqs_sent++;
		else {
			blks++;

			/* Since we blocked, give the system a chance to catch up, try again and drop if not ready
			 * This is where a queue would be useful
			 */
			usleep(1);
			rc = stk_data_flow_send(df,rcv_seq,STK_TCP_SEND_FLAG_NONBLOCK);
			if(rc != STK_SUCCESS) {
				drops++;
				if(!opts.quiet)
					STK_LOG(STK_LOG_NORMAL,"Failed to send data to remote service %d",rc);
			}
		}
	}
}

void usage()
{
	fprintf(stderr,"Usage: raw_multicast_bridge [options] <receive multicast ip:receive multicast port> <destination tcp ip:destination tcp port>\n");
	fprintf(stderr,"       -h                        : This help!\n");
	fprintf(stderr,"       -v                        : verbose mode - info per message\n");
	fprintf(stderr,"       -S <name>                 : Service Name\n");
	fprintf(stderr,"       -m <[protocol:]ip[:port]> : IP and port of monitor (default: tcp:127.0.0.1:20001)\n");
	fprintf(stderr,"                                 : protocol may be <tcp|udp>\n");
	fprintf(stderr,"       -R <[protocol:]ip[:port]> : IP and port of name server\n");
	fprintf(stderr,"                                 : protocol may be <tcp|udp>\n");
}

/* Process command line options */
int process_cmdline(int argc,char *argv[],struct cmdopts *opts )
{
	int rc;

	while(1) {
		rc = getopt(argc, argv, "hqs:S:m:R:");
		if(rc == -1) break;

		switch(rc) {
		case 'h': /* Help! */
			usage();
			exit(0);

		case 'm': /* IP/Port of monitoring daemon */
			process_monitoring_string(opts,optarg);
			break;

		case 'v': /* Be less verbose about whats happening */
			opts->quiet = 0;
			break;

		case 'S': /* Set the Service Name of this process */
			strncpy(opts->service_name,optarg,sizeof(opts->service_name));
			break;

		case 'R': /* Set the IP/Port of the Name Server */
			process_name_server_string(opts,optarg);
			break;

		}
	}

	/* Process mandatory options for multicast and tcp addresses and ports */
	if(optind + 2 > argc) return -1;

	{
	char *colon;

	opts->server_ip = argv[optind + 1];
	colon = strchr(argv[optind + 1],':');
	if(colon) {
		*colon = '\0';
		opts->server_port = ++colon;
	}
	else
		opts->server_port = "29090";

	opts->mcast_recv_addr = argv[optind];
	colon = strchr(argv[optind],':');
	if(colon) {
		*colon = '\0';
		opts->mcast_recv_port = ++colon;
	}
	else
		opts->mcast_recv_port = "29312";
	}

	return 0;
}

/* Callback to process name lookup responses */
void process_name_responses(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc = stk_name_service_invoke(rcv_seq);
	STK_ASSERT(rc==STK_SUCCESS,"invoke name service on sequence");
}

/* Callback to process monitoring responses */
void process_monitoring_responses(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	printf("Monitoring Response Sequence %p of type %u received\n",rcv_seq,stk_get_sequence_type(rcv_seq));
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

void name_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,fd_hangup_cb,process_name_responses);
	STK_ASSERT(added != -1,"Failed to add data flow to dispatcher");
}

void monitoring_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,fd_hangup_cb,process_data);
	STK_ASSERT(added != -1,"Failed to add data flow to dispatcher");
}

void data_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,NULL,process_data);
	STK_ASSERT(added != -1,"Failed to add data flow to dispatcher");
}

void fd_destroyed_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int removed = dispatch_remove_fd(default_dispatcher(),fd);
	STK_ASSERT(removed != -1,"Failed to remove data flow from dispatcher");
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

int main(int argc,char *argv[])
{
	stk_env_t *stkbase;
	stk_sequence_t *seq;
	stk_service_t *svc;
	stk_data_flow_t *mdf,*monitoring_df;
	stk_bool rc;
	stk_sequence_id snd_id,rcv_id;

	signal(SIGTERM, term); /* kill */
	signal(SIGINT, term);  /* ctrl-c */
	sigignore(SIGPIPE); /* Some system benefit from ignoring SIGPIPE */

	/* Set the default service name */
	strcpy(opts.service_name,"raw multicast bridge");
	opts.quiet = 1;
	/* Set the default monitoring protocol */
	strcpy(opts.monitor_protocol,"tcp");

	/* Process command line options */
	if(process_cmdline(argc,argv,&opts) == -1) {
		usage();
		exit(5);
	}

	/* Only log errors to stderr when running quiet */
	if(opts.quiet)
		stk_set_stderr_level(STK_LOG_ERROR);

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
		{ "data_flow_name", "name server data flow for throughput_test"}, { "data_flow_id", (void *) STK_NAME_SERVICE_DATA_FLOW_ID },
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
		{ "data_flow_name", "monitoring socket for throughput_test"}, { "data_flow_id", (void *) STK_HTTPD_DATA_FLOW_ID },
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


	/* Create our bridge service, creating a data flow shared by data and notifications,
	 * and enabling monitoring using the data flows options. The service will create a data flow for monitoring.
	 */
	{
	stk_options_t data_flow_options[] = {
		{ "connect_address", "127.0.0.1"}, {"connect_port", "29312"}, { "nodelay", (void *) STK_TRUE},
		{ "fd_created_cb", (void *) data_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb }, { NULL, NULL }
		};

	df = stk_tcp_client_create_data_flow(stkbase,"tcp client socket for raw_multicast_bridge", 29090, data_flow_options);
	STK_ASSERT(df!=NULL,"Failed to create tcp client data flow");

	{
	stk_options_t svc_auto_notify[] = { { "notification_data_flow", df }, {NULL,NULL} };
	stk_service_id svc_id;
	struct timeval tv;

	data_flow_options[0].data = opts.server_ip;
	data_flow_options[1].data = opts.server_port;

	gettimeofday(&tv,NULL);
	srand((unsigned int) (tv.tv_sec + tv.tv_usec)); 
	svc_id = rand();

	printf("Using random service ID: %ld\n",svc_id);

	svc = stk_create_service(stkbase,opts.service_name, svc_id, STK_SERVICE_TYPE_DATA, svc_auto_notify);
	STK_ASSERT(svc!=NULL,"Failed to create a basic named data service object");
	}
	}

	/* Now create the raw UDP multicast data flow to receive data on */
	{
	stk_options_t data_flow_options[] = {
		{ "bind_address", "0.0.0.0"}, {"bind_port", "29312"}, {"reuseaddr", NULL},
		{ "receive_buffer_size", "1024000" }, { "multicast_address", "224.10.10.20" },
		{ "fd_created_cb", (void *) data_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb }, { NULL, NULL } };

	data_flow_options[1].data = opts.mcast_recv_port;
	data_flow_options[4].data = opts.mcast_recv_addr;

	mdf = stk_rawudp_listener_create_data_flow(stkbase,"udp listener socket for data flow test",29190,data_flow_options);
	STK_ASSERT(df!=NULL,"Failed to create tcp client data flow");
	}


	/* Set this service to a running state so folks know we are in good shape */
	rc = stk_set_service_state(svc,STK_SERVICE_STATE_RUNNING);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to set service state of RUNNING : %d",rc);

	/* Create a sequence to send data with.
	 * Set this sequence type to DATA and the service type to DATA. Both these
	 * can be application defined and used as seen fit by the application.
	 */
	snd_id = stk_acquire_sequence_id(stkbase,STK_SERVICE_TYPE_DATA);
	seq = stk_create_sequence(stkbase,"raw_multicast_bridge sequence",0xfedcba98,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
	STK_ASSERT(seq!=NULL,"Failed to allocate sequence");

	/* Send the sequences and receive responses using the example dispatcher */
	STK_LOG(STK_LOG_NORMAL,"Receiving raw multicast sequences");
	{
	int iterations = 0;
	while(ending == 0) {
		client_dispatcher_timed(default_dispatcher(),stkbase,process_data,1000);

		/* Update this service's checkpoint so others know we are doing sane things */
		stk_service_update_smartbeat_checkpoint(svc,(stk_checkpoint_t) iterations++);

		/* Dump some stats about the bridge */
		printf("Received %d sent %d dropped %d blocked %d checkpoint %d\n",seqs_rcvd,seqs_sent,drops,blks,iterations);
	}
	}

	rc = stk_set_service_state(svc,STK_SERVICE_STATE_STOPPING);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to set service state of STOPPING : %d",rc);

	puts("Waiting for 5 seconds before closing");
	client_dispatcher_timed(default_dispatcher(),stkbase,process_data,5000);

	/* Free the dispatcher (and its related resources) */
	terminate_dispatcher(default_dispatcher());

	/* Don't need this data any more, destroy it and release its ID */
	rc = stk_destroy_sequence(seq);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a sequence");

	rc = stk_release_sequence_id(stkbase,snd_id);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to release the send sequence ID");

	/* Ok, now we can get rid of the service */
	rc = stk_destroy_service(svc,NULL);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the service : %d",rc);

	/* Let data drain before closing notification and monitoring data flows */
	puts("Draining for 5 seconds before closing");
	client_dispatcher_timed(default_dispatcher(),stkbase,process_data,5000);

	rc = stk_destroy_data_flow(mdf);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the multicast data flow : %d",rc);

	rc = stk_destroy_data_flow(df);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the data/notification data flow : %d",rc);

	/* And get rid of the environment, we are done! */
	rc = stk_destroy_env(stkbase);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	/* Output some basic info! */
	printf("%s: Sent and Received %d sequences\n",argv[0],seqs_sent);
	if(blks > 0)
		printf("I/O blocked attempts: %d",blks);

	return 0;
}

