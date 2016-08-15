/*
 * Copyright Dave Trollope 2015
 *
 * This file implements a more comprehensive client to allow you to test
 * the STK data flow APIs under various conditions such as specific payload sizes or
 * number of sequences.
 *
 * Much like simple_client, it creates a service and reports state and it does its work.
 * This example demonstrates the broader use of application state (e.g. "sending") and
 * this application state will be visible on the web monitor (stkhttpd)
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
#include "stk_udp_client_api.h"
#include "stk_rawudp_api.h"
#include "stk_name_service_api.h"
#include "stk_data_flow_api.h"
#include "stk_data_flow.h"
#include "stk_tcp.h"
#include "stk_udp.h"
#include "stk_ids.h"
#include "eg_dispatcher_api.h"

/* Use examples header for asserts */
#include "stk_examples.h"
char *default_buffer;

struct cmdopts {
	char service_name[128];
	int async_seqs;
	int seqs;
	int seqlen;
	int pause_ms;
	char quiet;
	char protocol;
	char passive;
	char *server_ip;
	char *server_port;
	char *server_name;
	/* See stk_examples.h */
	STK_NAME_SERVER_OPTS
	STK_MONITOR_OPTS
} opts;

int seqs_sent,seqs_rcvd;
int name_lookup_expired;
int name_lookup_cbs_rcvd;

#define STATE_SENDING 0x81
#define STATE_WAITING 0x82

stk_ret process_seq_segment(stk_sequence_t *seq, void *vdata, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	unsigned char *data = (unsigned char *) vdata;

	if(!opts.quiet) {
		printf("Sequence %p Received %ld bytes of type %ld\n",seq,sz,user_type);

		if(data)
			printf("First bytes: %2x %2x %2x %2x\n",data[0],data[1],data[2],data[3]);
	}

	return STK_SUCCESS;
}

/* Process the received sequence data */
void process_data(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc;

	STK_ASSERT(stk_get_sequence_type(rcv_seq) == STK_SEQUENCE_TYPE_DATA, "Received non data sequence in process_data callback");

	rc = stk_iterate_sequence(rcv_seq,process_seq_segment,NULL);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to process received sequencebuffer space to receive");

	seqs_rcvd++;

	if(seqs_rcvd == seqs_sent || (seqs_rcvd % opts.async_seqs == opts.async_seqs - 1)) {
		printf("Received: %d Sent: %d\n",seqs_rcvd,seqs_sent);
		stop_dispatching(default_dispatcher());
		wakeup_dispatcher(stk_env_from_data_flow(rcvchannel));
	}
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

	if(opts.server_name && strcmp(name_info->name,opts.server_name) == 0) {
		stk_uint16 *data_flow_type = NULL;
		stk_uint64 sz;
		stk_ret rc;

		/* Get the protocol for the server, if present */
		rc = stk_sequence_find_data_by_type(name_info->meta_data,STK_MD_DATA_FLOW_TYPE,(void **) &data_flow_type,&sz);
		if(rc == STK_SUCCESS) {
			switch(*data_flow_type) {
			case STK_TCP_SERVER_FLOW : opts.protocol = 0; break;
			case STK_RAWUDP_LISTENER_FLOW : opts.protocol = 1; break;
			case STK_UDP_LISTENER_FLOW : opts.protocol = 2; break;
			}
		}

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

void usage()
{
	fprintf(stderr,"Usage: throughput_test [options]\n");
	fprintf(stderr,"       -a #                      : Number of asyncronous sequences [default 10]\n");
	fprintf(stderr,"       -h                        : This help!\n");
	fprintf(stderr,"       -l #                      : Length of sequences\n");
	fprintf(stderr,"       -p #                      : Pause time between sends [default 0]\n");
	fprintf(stderr,"       -v                        : verbose mode - per message I/O\n");
	fprintf(stderr,"       -s #                      : Number of sequences [default 100]\n");
	fprintf(stderr,"       -S <name>                 : Service Name\n");
	fprintf(stderr,"       -i lookup:<name>          : Name of server (name server will be used to get the ip/port)\n");
	fprintf(stderr,"       or <[protocol:]ip[:port]> : IP and port of server (default: tcp:127.0.0.1:29312)\n");
	fprintf(stderr,"                                 : protocol may be <tcp|rawudp|udp|multicast>\n");
	fprintf(stderr,"                                 : 'multicast' is an alias for 'udp:224.10.10.20'\n");
	fprintf(stderr,"       -m lookup:<name>          : Lookup <name> to get the protocol/ip/port from the name server\n");
	fprintf(stderr,"       or <[protocol:]ip[:port]> : IP and port of monitor (default: tcp:127.0.0.1:20001)\n");
	fprintf(stderr,"                                 : protocol may be <tcp|udp>\n");
	fprintf(stderr,"       -R <[protocol:]ip[:port]> : IP and port of name server\n");
	fprintf(stderr,"                                 : protocol may be <tcp|udp>\n");
	fprintf(stderr,"       -0                        : 0 Responses (passive mode)\n");
}

int process_cmdline(int argc,char *argv[],struct cmdopts *opts )
{
	int rc;

	while(1) {
		rc = getopt(argc, argv, "0a:hi:m:vs:l:p:S:R:");
		if(rc == -1) return 0;

		switch(rc) {
		case 'h': /* Help! */
			usage();
			exit(0);

		case 'a': /* Number of messages to be sent asynchronously */
			opts->async_seqs = atoi(optarg);
			if(opts->async_seqs <= 0) {
				printf("Must be at least 1 asyncronous sequence\n");
				return -1;
			}
			break;

		case 'i': /* IP/Port of server to exchange data with */
			{
			stk_protocol_def_t pdef;
			stk_data_flow_parse_protocol_str(&pdef,optarg);

			if(pdef.ip[0] != '\0') opts->server_ip = strdup(pdef.ip);
			if(pdef.port[0] != '\0') opts->server_port = strdup(pdef.port);

			if(strcasecmp(pdef.protocol,"lookup") == 0)
				opts->server_name = strdup(pdef.name); /* default anyway, but for completeness */
			else
			if(strcasecmp(pdef.protocol,"tcp") == 0) opts->protocol = 0; /* default anyway, but for completeness */
			else
			if(strcasecmp(pdef.protocol,"rawudp") == 0) opts->protocol = 1;
			else
			if(strcasecmp(pdef.protocol,"udp") == 0) opts->protocol = 2;
			else
			if(strcasecmp(pdef.protocol,"multicast") == 0) opts->protocol = 3;

			break;
			}

		case 'm': /* IP/Port of monitoring daemon */
			process_monitoring_string(opts,optarg);
			break;

		case 'R': /* Set the IP/Port of the Name Server */
			process_name_server_string(opts,optarg);
			break;

		case 'l':
			opts->seqlen = atoi(optarg);
			break;

		case 'p':
			opts->pause_ms = atoi(optarg);
			break;

		case 's': /* Number of sequences to be sent and received */
			opts->seqs = atoi(optarg);
			break;

		case 'v': /* Be less verbose about whats happening */
			opts->quiet = 0;
			break;

		case 'S': /* Set the Service Name of this process */
			strncpy(opts->service_name,optarg,sizeof(opts->service_name));
			break;

		case '0': /* Passive mode - no responses expected */
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
	STK_ASSERT(removed != -1,"remove data flow from dispatcher");
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
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,fd_hangup_cb,process_data);
	STK_ASSERT(added != -1,"add data flow to dispatcher");
}

void dump_stats(struct timeval *start_tv,struct timeval *sent_tv,struct timeval *end_tv,int seqs)
{
	int diff_secs;
	int diff_usecs;

	diff_secs = sent_tv->tv_sec - start_tv->tv_sec;
	if(sent_tv->tv_usec < start_tv->tv_usec) {
		diff_usecs = (1000000 - start_tv->tv_usec) + sent_tv->tv_usec;
		diff_secs--;
	} else {
		diff_usecs = sent_tv->tv_usec - start_tv->tv_usec;
	}
	printf("Send Interval (sent - start): %d.%06d secs (avg %ld usecs)\n",diff_secs,diff_usecs,(((long)diff_secs*1000000) + (long)diff_usecs)/seqs);

	diff_secs = end_tv->tv_sec - start_tv->tv_sec;
	if(end_tv->tv_usec < start_tv->tv_usec) {
		diff_usecs = (1000000 - start_tv->tv_usec) + end_tv->tv_usec;
		diff_secs--;
	} else {
		diff_usecs = end_tv->tv_usec - start_tv->tv_usec;
	}
	printf("Total Interval (end - start): %d.%06d secs\n",diff_secs,diff_usecs);
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

	/* Set the default service name, number of sequences etc */
	strcpy(opts.service_name,"throughput_test service");
	opts.seqs = 100;
	/* Set the default monitoring protocol */
	strcpy(opts.monitor_protocol,"tcp");

	/* Set the number of messages to be sent before polling for received messages */
	opts.async_seqs = 10;

	/* Be quiet by default, user may request verbose output */
	opts.quiet = 1;

	if(process_cmdline(argc,argv,&opts) == -1) {
		usage();
		exit(5);
	}

	/* Only log errors to stderr when running quiet */
	if(opts.quiet)
		stk_set_stderr_level(STK_LOG_ERROR);

	/* Round off number of requested sequences to be a multiplier of the number of async sequences */
	opts.seqs -= (opts.seqs % opts.async_seqs);
	STK_ASSERT(opts.seqs > 0,"Number of sequences should be >= number of async messages %d",opts.async_seqs);

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

	if(opts.server_name) {
		stk_ret ret;

		name_lookup_cbs_rcvd = 0;
		name_lookup_expired = 0;

		/* Request the monitoring IP/Port from the name server */
		ret = stk_request_name_info(stk_env_get_name_service(stkbase), opts.server_name, 1000, name_info_cb, NULL, NULL);
		STK_ASSERT(ret==STK_SUCCESS,"Failed to request name '%s' %d",opts.monitor_name,ret);

		/* Dispatch to process name request response */
		while(name_lookup_cbs_rcvd == 0 && name_lookup_expired == 0)
			client_dispatcher_timed(default_dispatcher(),stkbase,NULL,100);
		if(name_lookup_expired == 1 && name_lookup_cbs_rcvd == 1) {
			printf("Could not resolve %s\n",opts.server_name);
			exit(5);
		}
		/* Wait for request to expire, for clarity following subsequent code and output */
		printf("Waiting to complete request\n");
		while(name_lookup_expired == 0)
			client_dispatcher_timed(default_dispatcher(),stkbase,NULL,500);
	}

	/* Create a data flow shared by data and notifications */
	switch(opts.protocol)
	{
	case 2:
	case 3:
		{
		stk_options_t data_flow_options[] = { { "destination_address", "127.0.0.1"}, {"destination_port", "29312"},
			{ "fd_created_cb", (void *) data_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb }, { NULL, NULL } };

		if(opts.protocol == 3)
			data_flow_options[0].data = "224.10.10.20"; /* Set default for multicast */

		if(opts.server_ip) data_flow_options[0].data = opts.server_ip;
		if(opts.server_port) data_flow_options[1].data = opts.server_port;

		df = stk_udp_client_create_data_flow(stkbase,"udp client socket for throughput_test", 29090, data_flow_options);
		STK_ASSERT(df!=NULL,"Failed to create udp client data flow");

		opts.passive = 1; /* Unidirectional transport, no returning messages */
		}
		break;

	case 1:
		{
		stk_options_t data_flow_options[] = { { "destination_address", "127.0.0.1"}, {"destination_port", "29312"},
			{ "fd_created_cb", (void *) data_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb }, { NULL, NULL } };

		if(opts.server_ip) data_flow_options[0].data = opts.server_ip;
		if(opts.server_port) data_flow_options[1].data = opts.server_port;

		df = stk_rawudp_client_create_data_flow(stkbase,"rawudp client socket for throughput_test", 29090, data_flow_options);
		STK_ASSERT(df!=NULL,"Failed to create rawudp client data flow");

		if(opts.seqlen == 0)
			opts.seqlen = 1; /* Can't send 0 bytes of raw data */

		opts.passive = 1; /* Unidirectional transport, no returning messages */
		}
		break;

	default:
		{
		stk_options_t data_flow_options[] = { { "connect_address", "127.0.0.1"}, {"connect_port", "29312"}, { "nodelay", (void*) STK_TRUE},
			{ "fd_created_cb", (void *) data_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb }, { NULL, NULL } };

		if(opts.server_ip) data_flow_options[0].data = opts.server_ip;
		if(opts.server_port) data_flow_options[1].data = opts.server_port;

		df = stk_tcp_client_create_data_flow(stkbase,"tcp client socket for throughput_test", 29090, data_flow_options);
		STK_ASSERT(df!=NULL,"Failed to create tcp client data flow");
		}
		break;
	}

	{
	stk_options_t svc_auto_notify[] = { { "notification_data_flow", df }, {NULL,NULL} };
	stk_service_id svc_id;
	struct timeval tv;

	gettimeofday(&tv,NULL);
	srand((unsigned int) (tv.tv_sec + tv.tv_usec)); 
	svc_id = rand();

	printf("Using random service ID: %lu\n",svc_id);

	svc = stk_create_service(stkbase,opts.service_name, svc_id, STK_SERVICE_TYPE_DATA, svc_auto_notify);
	STK_ASSERT(svc!=NULL,"Failed to create a basic named data service object");
	}

	/* Add a state of "sending" and "waiting" to this service */
	stk_set_service_state_str(svc,STATE_SENDING,"sending",sizeof("sending"));
	stk_set_service_state_str(svc,STATE_WAITING,"waiting",sizeof("waiting"));

	rc = stk_set_service_state(svc,STK_SERVICE_STATE_RUNNING);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to set service state of RUNNING : %d",rc);

	snd_id = stk_acquire_sequence_id(stkbase,STK_SERVICE_TYPE_DATA);
	seq = stk_create_sequence(stkbase,"throughput_test sequence",snd_id,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
	STK_ASSERT(seq!=NULL,"Failed to allocate test sequence");

	/* Create a sequence to receive data, and wait for it */
	rcv_id = stk_acquire_sequence_id(stkbase,STK_SERVICE_TYPE_DATA);
	ret_seq = stk_create_sequence(stkbase,"throughput_test return sequence",rcv_id,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
	STK_ASSERT(ret_seq!=NULL,"Failed to allocate test return sequence");

	/* STK supports data-full and data-less sequences. */
	if(opts.seqlen > 0) {
		default_buffer = malloc(opts.seqlen);
		for(unsigned int i = 0; i < opts.seqlen/sizeof(unsigned long); i++)
			*((unsigned long *) &default_buffer[i * sizeof(unsigned long)]) = 0x0123456789abcdef;
		strncpy(default_buffer,"default data",opts.seqlen < 12 ? opts.seqlen : 12);
		default_buffer[opts.seqlen-2] = (char) 0xde; /* Mark the end */
		default_buffer[opts.seqlen-1] = (char) 0xad; /* Mark the end */
		rc = stk_add_reference_to_sequence(seq,default_buffer,opts.seqlen,0x135);
		STK_ASSERT(rc==STK_SUCCESS,"Failed to add reference data, size %d to sequence",opts.seqlen);
	}

	{
	int blks = 0,flags = opts.protocol == 0 ? STK_TCP_SEND_FLAG_NONBLOCK : STK_UDP_SEND_FLAG_NONBLOCK;
	struct timeval start_tv,sent_tv;

	/* Send data */
	STK_LOG(STK_LOG_NORMAL,"Sending %d sequences",opts.seqs);

	gettimeofday(&start_tv,NULL);

	rc = stk_set_service_state(svc,STATE_SENDING);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to set service state of sending : %d",rc);

	for(int i = 0; seqs_sent < opts.seqs; i++) {
		if(opts.pause_ms > 0) {
			/* Pause between sends but keep dispatching - don't want to block that!! */
			client_dispatcher_hard_timed(default_dispatcher(),stkbase,NULL,opts.pause_ms);
		}

		rc = stk_data_flow_send(df,seq,flags);
		if(rc == STK_SUCCESS || rc == STK_WOULDBLOCK) {
			if(rc == STK_SUCCESS) {
				seqs_sent++;
				stk_service_update_smartbeat_checkpoint(svc,(stk_checkpoint_t) seqs_sent);
			}

			/* Batch up dispatches to every opts.async_seqs sequences or when blocked */
			if(rc == STK_WOULDBLOCK || (rc == STK_SUCCESS && seqs_sent % opts.async_seqs == opts.async_seqs - 1))
				client_dispatcher_poll(default_dispatcher(),stkbase,NULL);

			if(rc != STK_SUCCESS) {
				blks++;
				/* Since we blocked, give the system a chance to catch up */
				usleep(1);
			}
		} else {
			if(!opts.quiet)
				STK_LOG(STK_LOG_NORMAL,"Failed to send data to create remote service %d",rc);
		}
	}
	rc = stk_set_service_state(svc,STATE_WAITING);
	STK_LOG(STK_LOG_NORMAL,"Waiting for responses");
	STK_ASSERT(rc==STK_SUCCESS,"Failed to set service state of waiting : %d",rc);
	gettimeofday(&sent_tv,NULL);

	rc = stk_set_service_state(svc,STK_SERVICE_STATE_STOPPING);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to set service state of STOPPING : %d",rc);

	if(opts.passive == 0) {
		puts("Waiting for 10 seconds (or until all messages are received) before closing");

		int count = 0;
		do {
			client_dispatcher_hard_timed(default_dispatcher(),stkbase,NULL,100);
			count++;
		} while (count < 100 && seqs_rcvd < opts.seqs);
	}
	if(seqs_rcvd >= opts.seqs || opts.passive) {
		struct timeval end_tv;

		gettimeofday(&end_tv,NULL);

		dump_stats(&start_tv,&sent_tv,&end_tv,opts.seqs);
	}

	terminate_dispatcher(default_dispatcher());

	/* Don't need this data any more, destroy it */
	rc = stk_destroy_sequence(seq);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a sequence");

	rc = stk_release_sequence_id(stkbase,snd_id);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to release the send sequence ID");

	/* Ok, we received something, we are done, lets delete everything */
	rc = stk_destroy_sequence(ret_seq);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a received sequence");

	rc = stk_release_sequence_id(stkbase,rcv_id);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to release the receive sequence ID");

	rc = stk_destroy_service(svc,NULL);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the service : %d",rc);

	/* Let data drain before closing notification and monitoring data flows */
	puts("Service destroyed, letting data flows drain");
	client_dispatcher_timed(default_dispatcher(),stkbase,NULL,5000);

	rc = stk_destroy_data_flow(df);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the data/notification data flow : %d",rc);

	rc = stk_destroy_env(stkbase);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	if(default_buffer) free(default_buffer);

	if(seqs_rcvd < opts.seqs && opts.passive == 0)
		printf("ERROR: Never received all the responses (%d sent %d received)\n",seqs_sent,seqs_rcvd);
	else if(opts.passive == 0)
		printf("%s ended: Sent and Received %d sequences\n",argv[0],seqs_sent);
	else
		printf("%s ended: Sent %d sequences\n",argv[0],seqs_sent);
	if(blks > 0)
		printf("I/O blocked attempts: %d\n",blks);
	}
	return 0;
}

