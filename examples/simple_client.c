/*
 * Copyright Dave Trollope 2015
 *
 * This file implements a basic service and is intended to be a simple introduction
 * to the STK for client type applications. It is intended to be used with simple_server.
 *
 * This example queries the name server for a service name, sets up service monitoring,
 * creates a service and creates a TCP data flow to connect to the (simple_)server.
 *
 * It sets the service state to RUNNING and sends sequences to the server which reflects
 * them back.
 *
 * Before closing, the service state is set to STOPPING and then the example exits.
 *
 * This example uses several macros defined in stk_examples.h to simplify understanding and
 * keep focus on the most important details.
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <signal.h>
#ifdef __APPLE__
/* This should not be needed, but darwin still reports implicit declaration of strdup */
char *strdup(const char *);
#endif
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_sequence_api.h"
#include "stk_tcp_client_api.h"
#include "stk_name_service_api.h"
#include "stk_service_group_api.h"
#include "stk_sg_automation_api.h"
#include "stk_data_flow_api.h"
#include "stk_tcp.h"
#include "stk_ids.h"
#include "eg_dispatcher_api.h"

/* This example batches up sequences to be processed for efficiency */
#define BATCH_SIZE 5

/* Use examples header for asserts */
#include "stk_examples.h"

/* Default data buffer */
char default_buffer[50];

/* command line options provided - set in process_cmdline() */
struct cmdopts {
	char service_name[128];
	int seqs;
	short quiet;
	char *server_ip;
	char *server_port;
	char *server_name;
	/* See stk_examples.h */
	STK_NAME_SERVER_OPTS
	STK_MONITOR_OPTS
} opts;

/* Simple stat */
int seqs_rcvd;
int name_lookup_expired;
int name_lookup_cbs_rcvd;

/* Callback to process a segment of a Sequence.
 * A Sequence may contain multiple segments and using a callback like this with stk_iterate_sequence()
 * allows cleanly structured code to handle each segment. This model also allows for iteration over merged
 * sequences. See stk_add_sequence_reference_in_sequence() and stk_iterate_sequence();
 */
stk_ret process_seq_segment(stk_sequence_t *seq, void *vdata, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	unsigned char *data = (unsigned char *) vdata;

	if(!opts.quiet) {
		printf("Sequence %p received %ld bytes of type %lu\n",seq,sz,user_type);

		if(data && sz >= 4) {
			printf("Bytes: %02x %02x %02x %02x ... %02x %02x %02x %02x\n",
				data[0],data[1],data[2],data[3],data[sz - 4],data[sz - 3],data[sz - 2],data[sz - 1]);
		}
	}

	return STK_SUCCESS;
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

/* Callback to process received sequence data */
void process_data(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc;

	STK_ASSERT(stk_get_sequence_type(rcv_seq) == STK_SEQUENCE_TYPE_DATA, "Received non data sequence in process_data callback");

	/* Iterate over each segment and call process_seq_segment() for each */
	rc = stk_iterate_sequence(rcv_seq,process_seq_segment,NULL);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to process received sequencebuffer space to receive");

	/* Count the sequence as received */
	seqs_rcvd++;

	/* Stop if we've received enough sequences (or reached our batch size) from the server and wakeup
	 * the example dispatching so it can cleanly exit. Or, loop if we have received enough sequences
	 * to batch process them
	 */
	if(seqs_rcvd == opts.seqs || (seqs_rcvd % BATCH_SIZE == BATCH_SIZE - 1)) {
		stop_dispatching(default_dispatcher());
		wakeup_dispatcher(stk_env_from_data_flow(rcvchannel));
	}
}

void usage()
{
	fprintf(stderr,"Usage: simple_client [options]\n");
	fprintf(stderr,"       -h                        : This help!\n");
	fprintf(stderr,"       -i lookup:<name>          : Name of server (name server will be used to get the ip/port)\n");
	fprintf(stderr,"                                 : default: 'Simple Server Service Group'");
	fprintf(stderr,"       -q                        : quiet mode - no per message I/O\n");
	fprintf(stderr,"       -s #                      : Number of sequences\n");
	fprintf(stderr,"       -S <name>                 : Service Name\n");
	fprintf(stderr,"       -m lookup:<name>          : Lookup <name> to get the protocol/ip/port from the name server\n");
	fprintf(stderr,"       or <[protocol:]ip[:port]> : IP and port of monitor (default: tcp:127.0.0.1:20001)\n");
	fprintf(stderr,"                                 : protocol may be <tcp|udp>\n");
	fprintf(stderr,"       -R <[protocol:]ip[:port]> : IP and port of name server\n");
	fprintf(stderr,"                                 : protocol may be <tcp|udp>\n");
}

/* Process command line options */
int process_cmdline(int argc,char *argv[],struct cmdopts *opts )
{
	int rc;

	while(1) {
		rc = getopt(argc, argv, "hqs:S:i:m:R:");
		if(rc == -1) return 0;

		switch(rc) {
		case 'h': /* Help! */
			usage();
			exit(0);

		case 'i': /* IP/Port of server to exchange data with */
			{
			char *colon;

			if(strncmp("lookup:",optarg,7)) {
				printf("Please use 'lookup:<name>' instead of %s\n",optarg);
				exit(5);
			} else {
				opts->server_name = &optarg[7];
			}
			}
			break;

		case 'm': /* IP/Port of monitoring daemon */
			process_monitoring_string(opts,optarg);
			break;

		case 'R': /* Set the IP/Port of the Name Server */
			process_name_server_string(opts,optarg);
			break;

		case 's': /* Number of sequences to be sent and received */
			opts->seqs = atoi(optarg);
			break;

		case 'q': /* Be less verbose about whats happening */
			opts->quiet = 1;
			break;

		case 'S': /* Set the Service Name of this process */
			strncpy(opts->service_name,optarg,sizeof(opts->service_name));
			break;

		}
	}
	return 0;
}

void name_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,NULL,process_name_responses);
	STK_ASSERT(added != -1,"Failed to add name server data flow to dispatcher");
}

void monitoring_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,NULL,process_monitoring_responses);
	STK_ASSERT(added != -1,"Failed to add monitoring data flow (fd %d) to dispatcher",fd);
}

void data_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,NULL,process_data);
	STK_ASSERT(added != -1,"Failed to add data flow (fd %d) to dispatcher",fd);
}

void fd_destroyed_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int removed = dispatch_remove_fd(default_dispatcher(),fd);
	STK_ASSERT(removed != -1,"Failed to remove data flow (fd %d) from dispatcher",fd);
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

	if(name_info->ip[0].protocol[0] && strcmp(name_info->ip[0].protocol,"tcp")) {
		printf("Ignoring protocol '%s'\n",name_info->ip[0].protocol[0] ? name_info->ip[0].protocol : "unknown");
		return; /* Only support tcp */
	}

	printf("Received info on name %s, IP %s Port %d Protocol %s\n",name_info->name,name_info->ip[0].ipstr,
		name_info->ip[0].sockaddr.sin_port,name_info->ip[0].protocol[0] ? name_info->ip[0].protocol : "unknown");

	if(strcmp(name_info->name,opts.server_name) == 0) {
		opts.server_ip = strdup(name_info->ip[0].ipstr);
		opts.server_port = strdup(name_info->ip[0].portstr);
	} else
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
	stk_sequence_t *ret_seq;
	stk_service_t *svc;
	stk_data_flow_t *df,*monitoring_df;
	stk_bool rc;
	stk_sequence_id snd_id,rcv_id;

	sigignore(SIGPIPE); /* Some system benefit from ignoring SIGPIPE */

	/* Set the default number of sequences we want to send and receive,
	 * command line processing may override this
	 */
	opts.seqs = 100;
	/* Set the default service name */
	strcpy(opts.service_name,"simple_client service");
	/* Set the default monitoring protocol */
	strcpy(opts.monitor_protocol,"tcp");
	/* Set the default service group name. */
	opts.server_name = "Simple Server Service Group";

	/* Process command line options */
	if(process_cmdline(argc,argv,&opts) == -1) {
		usage();
		exit(5);
	}

	/* Only log errors to stderr when running quiet */
	if(opts.quiet)
		stk_set_stderr_level(STK_LOG_ERROR);

	/* Round off number of requested sequences to be a multiplier of the batch size */
	opts.seqs -= (opts.seqs % BATCH_SIZE);

	/* Create the STK environment - can't do anything without one. Configure the
	 * address/port of the name server and monitoring web server. Both are optional,
	 * and the monitoring configuration may be passed to service creation but for
	 * convenience we configure it in the env so it can be shared. Connections
	 * will be established to the servers. See simple_server.c for an example
	 * of creating the env without configuring these.
	 */
	{
	stk_options_t name_server_data_flow_options[] = { { "destination_address", "127.0.0.1"}, {"destination_port", "20002"}, { "nodelay", (void *)STK_TRUE},
		{ "data_flow_name", "name server data flow for simple_client"}, { "data_flow_id", (void *) STK_NAME_SERVICE_DATA_FLOW_ID },
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
		{ "data_flow_name", "monitoring socket for simple_client"}, { "data_flow_id", (void *) STK_HTTPD_DATA_FLOW_ID },
		{ "destination_address", "127.0.0.1"}, {"destination_port", "20001"}, { "nodelay", (void *) STK_TRUE},
		{ "fd_created_cb", (void *) monitoring_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb },
		{ NULL, NULL }
		};
	stk_options_t monitoring_options[] = { 
		{ "monitoring_data_flow_protocol", opts.monitor_protocol }, { "monitoring_data_flow_options", &monitoring_data_flow_options }, 
		{ NULL, NULL } };

	if(opts.monitor_name) 
		name_lookup_and_dispatch(stkbase, opts.monitor_name,
			name_info_cb, name_lookup_expired, name_lookup_cbs_rcvd, 0);

	if(opts.monitor_ip) monitoring_data_flow_options[2].data = opts.monitor_ip;
	if(opts.monitor_port) monitoring_data_flow_options[3].data = opts.monitor_port;

	rc = stk_set_env_monitoring_data_flow(stkbase,monitoring_options);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to set environment monitoring data flow");
	}

	name_lookup_and_dispatch(stkbase, opts.server_name, name_info_cb, name_lookup_expired, name_lookup_cbs_rcvd,0);

	/* Create our simple client service, creating a data flow shared by data and notifications,
	 * and enabling monitoring using the data flows options. The service will create a data flow for monitoring.
	 */
	{
	stk_options_t data_flow_options[] = {
		{ "destination_address", "127.0.0.1"}, {"destination_port", "29312"}, { "nodelay", (void *) STK_TRUE},
		{ "fd_created_cb", (void *) data_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb }, { NULL, NULL }
		};

	if(opts.server_ip) data_flow_options[0].data = opts.server_ip;
	if(opts.server_port) data_flow_options[1].data = opts.server_port;

	df = stk_tcp_client_create_data_flow(stkbase,"tcp client socket for simple_client", 29090, data_flow_options);
	STK_ASSERT(df!=NULL,"Failed to create tcp client data flow");

	{
	stk_options_t svc_auto_notify[] = { { "notification_data_flow", df }, {NULL,NULL} };
	stk_service_id svc_id;
	struct timeval tv;

	gettimeofday(&tv,NULL);
	srand((unsigned int) (tv.tv_sec + tv.tv_usec)); 
	svc_id = rand();

	printf("Using random service ID: %ld\n",svc_id);

	svc = stk_create_service(stkbase,opts.service_name, svc_id, STK_SERVICE_TYPE_DATA, svc_auto_notify);
	STK_ASSERT(svc!=NULL,"Failed to create a basic named data service object");
	}
	}

	/* Set this service to a running state so folks know we are in good shape */
	rc = stk_set_service_state(svc,STK_SERVICE_STATE_RUNNING);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to set service state of RUNNING : %d",rc);

	/* Create a sequence to send data with.
	 * Set this sequence type to DATA and the service type to DATA. Both these
	 * can be application defined and used as seen fit by the application.
	 */
	snd_id = stk_acquire_sequence_id(stkbase,STK_SERVICE_TYPE_DATA);
	seq = stk_create_sequence(stkbase,"simple_client sequence",0xfedcba98,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
	STK_ASSERT(seq!=NULL,"Failed to allocate sequence");

	/* Create a sequence to receive data */
	rcv_id = stk_acquire_sequence_id(stkbase,STK_SERVICE_TYPE_DATA);
	ret_seq = stk_create_sequence(stkbase,"simple_client return sequence",0xfedcba99,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
	STK_ASSERT(ret_seq!=NULL,"Failed to allocate return sequence");

	/* Set the default data to be sent */
	strcpy(default_buffer,"default data");

	/* Use a *reference* data segment in the sequence.
	 * Reference segments allow applications to change the data to be sent without
	 * API calls to the toolkit, but the application bears the responsibility
	 * for coordinating data updates and sends to maintain data integrity.
	 */
	rc = stk_add_reference_to_sequence(seq,default_buffer,strlen(default_buffer) + 1,0x135);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to add reference data to sequence");

	/* Send the sequences and receive responses using the example dispatcher */
	{
	int seqs_sent = 0,blks = 0;

	/* Send data over TCP */
	STK_LOG(STK_LOG_NORMAL,"Sending %d sequences",opts.seqs);
	for(int i = 0; seqs_sent < opts.seqs; i++) {
		/* Update the data to be sent. Note, because the sequence
		 * is using a reference segment, all we need to do is write
		 * to our application buffer.
		 */
		sprintf(default_buffer,"sequence %d",i);

		/* Send the sequence! */
		rc = stk_data_flow_send(df,seq,STK_TCP_SEND_FLAG_NONBLOCK);
		if(rc == STK_SUCCESS || rc == STK_WOULDBLOCK) {
			if(rc == STK_SUCCESS) {
				seqs_sent++;
				/* Update this service's checkpoint so others know we are doing sane things */
				stk_service_update_smartbeat_checkpoint(svc,(stk_checkpoint_t) seqs_sent);
			}

			/* Batch up dispatches to every BATCH_SIZE sequences or when blocked */
			if(rc == STK_WOULDBLOCK || (rc == STK_SUCCESS && seqs_sent % BATCH_SIZE == BATCH_SIZE - 1))
				client_dispatcher_timed(default_dispatcher(),stkbase,NULL,10);

			if(rc != STK_SUCCESS) {
				blks++;
				/* Since we blocked, give the system a chance to catch up */
				usleep(1);
			}
		} else {
			if(!opts.quiet)
				STK_LOG(STK_LOG_NORMAL,"Failed to send data to remote service %d",rc);
		}
	}
	/* Dispatch until we've received the same number of sequences as we sent */
	while(seqs_rcvd < opts.seqs) {
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
		printf("Received %d sequences, waiting for %d\n",seqs_rcvd,opts.seqs);
	}

	rc = stk_set_service_state(svc,STK_SERVICE_STATE_STOPPING);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to set service state of STOPPING : %d",rc);

	puts("Waiting for 5 seconds before closing");
	client_dispatcher_timed(default_dispatcher(),stkbase,NULL,5000);

	/* Free the dispatcher (and its related resources) */
	terminate_dispatcher(default_dispatcher());

	/* Don't need this data any more, destroy it and release its ID */
	rc = stk_destroy_sequence(seq);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a sequence");

	rc = stk_release_sequence_id(stkbase,snd_id);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to release the send sequence ID");

	/* Ok, we received something, release its ID */
	rc = stk_destroy_sequence(ret_seq);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a received sequence");

	rc = stk_release_sequence_id(stkbase,rcv_id);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to release the receive sequence ID");

	/* Ok, now we can get rid of the service */
	rc = stk_destroy_service(svc,NULL);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the service : %d",rc);

	/* Let data drain before closing notification and monitoring data flows */
	puts("Draining for 5 seconds before closing");
	client_dispatcher_timed(default_dispatcher(),stkbase,NULL,5000);

	rc = stk_destroy_data_flow(df);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the data/notification data flow : %d",rc);

	/* And get rid of the environment, we are done! */
	rc = stk_destroy_env(stkbase);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	/* Output some basic info! */
	printf("%s: Sent and Received %d sequences\n",argv[0],seqs_sent);
	if(blks > 0)
		printf("I/O blocked attempts: %d",blks);
	}
	return 0;
}

