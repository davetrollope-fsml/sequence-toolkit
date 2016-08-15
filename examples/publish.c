/*
 * Copyright Dave Trollope 2015
 *
 * This file implements a basic publisher.
 * It supports TCP and UDP (Raw, Unicast and Multicast) data flows.
 *
 * This example registers the publisher name with the name server and includes the data
 * flow IP/Port. Subscribers use name subscriptions to receive the IP/Port info from the
 * name server and join the advertised address.
 *
 * TCP Publishers are listening data flows and use the TCP Server API. UDP Publishers
 * rely on the UDP Client API to publish.
 *
 * However, for UDP (Unicast) this implicitly dictates the IP and Port the subscriber needs
 * to join on which, due to the nature of UDP prevents multiple subscribers joining the publisher.
 * Unidirectional/directed flows like this are still useful because they decouple the application
 * subscribing from needing to be configured with the IP/Port.
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
#include "stk_sequence_api.h"
#include "stk_tcp_client_api.h"
#include "stk_tcp_server_api.h"
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
	int async_seqs;
	int seqs;
	int seqlen;
	int pause_ms;
	char quiet;
	char protocol;
	char *publisher_ip;
	char *publisher_port;
	char *publisher_protocol;
	char *publisher_name;
	/* See stk_examples.h */
	STK_NAME_SERVER_OPTS
	STK_MONITOR_OPTS
	STK_BIND_OPTS
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
	printf("Meta data type %lu sz %lu\n",user_type,sz);
	return STK_SUCCESS;
}

void name_info_cb(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info,stk_name_info_cb_type cb_type)
{
	if(cb_type == STK_NS_REQUEST_EXPIRED) {
		name_lookup_cbs_rcvd++;
		name_lookup_expired = 1;
		printf("Request expired on name %s, %d callbacks received\n",name_info->name,name_lookup_cbs_rcvd);
		return;
	}

	printf("Received info on name %s, IP %s Port %d Protocol %s\n",name_info->name,name_info->ip[0].ipstr,
		name_info->ip[0].sockaddr.sin_port,name_info->ip[0].protocol[0] ? name_info->ip[0].protocol : "unknown");

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

void usage()
{
	fprintf(stderr,"Usage: publish [options] <name>\n");
	fprintf(stderr,"       -a #                      : Number of asyncronous sequences [default 10]\n");
	fprintf(stderr,"       -h                        : This help!\n");
	fprintf(stderr,"       -l #                      : Length of sequences\n");
	fprintf(stderr,"       -p #                      : Pause time between sends [default 0]\n");
	fprintf(stderr,"       -v                        : verbose mode - per message I/O\n");
	fprintf(stderr,"       -s #                      : Number of sequences [default 100]\n");
	fprintf(stderr,"       -i <[protocol:]ip[:port]> : IP and port of publisher (default: tcp:127.0.0.1:29312)\n");
	fprintf(stderr,"                                 : protocol may be <tcp|rawudp|udp|multicast>\n");
	fprintf(stderr,"                                 : 'multicast' is an alias for 'udp:224.10.10.20'\n");
	fprintf(stderr,"       -B ip[:port]              : IP and port to be bound (default: 0.0.0.0:29312)\n");
	fprintf(stderr,"       -R <[protocol:]ip[:port]> : IP and port of name server\n");
	fprintf(stderr,"                                 : protocol may be <tcp|udp>\n");
}

int process_cmdline(int argc,char *argv[],struct cmdopts *opts )
{
	int rc;

	while(1) {
		rc = getopt(argc, argv, "0a:B:hi:vs:l:p:S:R:");
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

		case 'B': /* Set the IP/Port to bind to */
			process_bind_string(opts,optarg);
			break;

		case 'i': /* IP/Port of server to exchange data with */
			{
			stk_protocol_def_t pdef;
			stk_data_flow_parse_protocol_str(&pdef,optarg);

			if(pdef.ip[0] != '\0') opts->publisher_ip = strdup(pdef.ip);
			if(pdef.port[0] != '\0') opts->publisher_port = strdup(pdef.port);
			if(pdef.protocol[0] != '\0') opts->publisher_protocol = strdup(pdef.protocol);

			if(strcasecmp(pdef.protocol,"tcp") == 0) opts->protocol = 0; /* default anyway, but for completeness */
			else
			if(strcasecmp(pdef.protocol,"rawudp") == 0) opts->protocol = 1;
			else
			if(strcasecmp(pdef.protocol,"udp") == 0) opts->protocol = 2;
			else
			if(strcasecmp(pdef.protocol,"multicast") == 0) opts->protocol = 3;

			break;
			}

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

int data_connection_count;
stk_data_flow_t *data_connections[5];
void fd_destroyed_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int removed = dispatch_remove_fd(default_dispatcher(),fd);
	STK_ASSERT(removed != -1,"remove data flow from dispatcher");

	{
	int shift = 0;
	for(int idx = 0;idx < data_connection_count;idx++) {
		if(shift)
			data_connections[idx - 1] = data_connections[idx];

		if(data_connections[idx] == flow) {
			data_connections[idx] = NULL;
			shift = 1;
		}
	}
	if(shift) data_connection_count--;
	}
}

void name_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,fd_hangup_cb,process_name_responses);
	STK_ASSERT(added != -1,"Failed to add data flow to dispatcher");
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

	if(stk_get_data_flow_type(flow) == STK_TCP_SERVER_FLOW)
		return;

	printf("Adding df %p fd %d to data connection table, current size %d\n",flow,fd,data_connection_count);
	data_connections[data_connection_count] = flow;
	data_connection_count++;
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
	stk_data_flow_t *df;
	stk_bool rc;
	stk_sequence_id snd_id,rcv_id;

	sigignore(SIGPIPE); /* Some system benefit from ignoring SIGPIPE */

	/* Set the default number of sequences, data length etc */
	opts.seqs = 100;
	opts.seqlen = 10;

	/* Set the number of messages to be sent before polling for received messages */
	opts.async_seqs = 10;

	/* Be quiet by default, user may request verbose output */
	opts.quiet = 1;

	if(process_cmdline(argc,argv,&opts) == -1 || argc <= optind) {
		usage();
		exit(5);
	}

	opts.publisher_name = argv[optind];
	if(!opts.publisher_name) {
		usage();
		exit(5);
	}
	printf("Publishing to '%s'\n",opts.publisher_name);

	/* Only log errors to stderr when running quiet */
	if(opts.quiet)
		stk_set_stderr_level(STK_LOG_ERROR);

	/* Round off number of requested sequences to be a multiplier of the number of async sequences */
	opts.seqs -= (opts.seqs % opts.async_seqs);
	STK_ASSERT(opts.seqs > 0,"Number of sequences should be >= number of async messages %d",opts.async_seqs);

	/* Create the STK environment - can't do anything without one. Configure the
	 * address/port of the name server.
	 */
	{
	stk_options_t name_server_data_flow_options[] = {
		{ "destination_address", "127.0.0.1"}, {"destination_port", "20002"}, { "nodelay", (void *)STK_TRUE},
		{ "data_flow_name", "name server data flow for publish"}, { "data_flow_id", (void *) STK_NAME_SERVICE_DATA_FLOW_ID },
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

	/* Create a data flow shared by data and notifications */
	switch(opts.protocol)
	{
	case 2:
	case 3:
		{
		stk_options_t data_flow_options[] = { { "destination_address", "127.0.0.1"}, {"destination_port", "29312"},
			{ "fd_created_cb", (void *) data_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb }, { NULL, NULL } };

		if(opts.protocol == 3 && !opts.publisher_ip)
				opts.publisher_ip = strdup("224.10.10.20");

		if(opts.publisher_ip) data_flow_options[0].data = opts.publisher_ip;
		if(opts.publisher_port) data_flow_options[1].data = opts.publisher_port;

		df = stk_udp_create_publisher(stkbase,"udp publisher data flow", 29090, data_flow_options);
		STK_ASSERT(df!=NULL,"Failed to create udp publisher data flow");
		}
		break;

	case 1:
		{
		stk_options_t data_flow_options[] = { { "destination_address", "127.0.0.1"}, {"destination_port", "29312"},
			{ "fd_created_cb", (void *) data_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb }, { NULL, NULL } };

		if(opts.publisher_ip) data_flow_options[0].data = opts.publisher_ip;
		if(opts.publisher_port) data_flow_options[1].data = opts.publisher_port;

		df = stk_rawudp_create_publisher(stkbase,"rawudp publisher data flow", 29090, data_flow_options);
		STK_ASSERT(df!=NULL,"Failed to create rawudp publisher data flow");
		}
		break;

	default:
		{
		stk_options_t tcp_options[] = { { "bind_address", "0.0.0.0"}, {"bind_port", "29312"}, {"nodelay", NULL},
			{ "send_buffer_size", "800000" }, { "receive_buffer_size", "16000000" },{ "reuseaddr", (void *) 1 },
			{ "fd_created_cb", (void *) data_fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb },
			{ NULL, NULL } };

		if(opts.bind_ip)
			tcp_options[0].data = opts.bind_ip;
		if(opts.publisher_port)
			tcp_options[1].data = opts.publisher_port;

		df = stk_tcp_server_create_data_flow(stkbase,"tcp publisher data flow", STK_EG_SERVER_DATA_FLOW_ID, tcp_options);
		STK_ASSERT(df!=NULL,"Failed to create tcp publisher data flow");
		}
		break;
	}

	/* This is registering 127.0.0.1:29312 as the IP/Port associated with this name - its not the server we are connecting to */
	stk_options_t name_options[] = { { "destination_address", "127.0.0.1"}, {"destination_port", "29312"}, { "destination_protocol", "tcp" }, { "fault_tolerant_state", "active"}, { NULL, NULL } };

	if(opts.publisher_ip) name_options[0].data = opts.publisher_ip;
	if(opts.publisher_port) name_options[1].data = opts.publisher_port;
	if(opts.publisher_protocol) name_options[2].data = opts.publisher_protocol;

	name_lookup_cbs_rcvd = 0;
	name_lookup_expired = 0;

	rc = stk_register_name(stk_env_get_name_service(stkbase), opts.publisher_name,
		/* Linger (sec) */ 5, /* Expiration (ms) */ 1000 , name_info_cb, NULL, name_options);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to register name");

	/* Dispatch to process name request response */
	while(name_lookup_cbs_rcvd == 0 && name_lookup_expired == 0)
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,100);
	if(name_lookup_expired == 1 && name_lookup_cbs_rcvd == 1) {
		printf("Could not resolve %s\n",opts.publisher_name);
		exit(5);
	}
	/* Wait for request to expire, for clarity following subsequent code and output */
	printf("Waiting to complete request\n");
	while(name_lookup_expired == 0)
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,500);

	snd_id = stk_acquire_sequence_id(stkbase,STK_SERVICE_TYPE_DATA);
	seq = stk_create_sequence(stkbase,"publish sequence",snd_id,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
	STK_ASSERT(seq!=NULL,"Failed to allocate test sequence");

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

	printf("Waiting for connections\n");
	while(data_connection_count == 0)
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,500);

	printf("Something connected!\n");

	{
	int blks = 0,flags = opts.protocol == 0 ? STK_TCP_SEND_FLAG_NONBLOCK : STK_UDP_SEND_FLAG_NONBLOCK;
	struct timeval start_tv,sent_tv;

	/* Send data */
	STK_LOG(STK_LOG_NORMAL,"Sending %d sequences",opts.seqs);

	gettimeofday(&start_tv,NULL);

	for(int i = 0; seqs_sent < opts.seqs; i++) {
		if(opts.pause_ms > 0) {
			/* Pause between sends but keep dispatching - don't want to block that!! */
			client_dispatcher_hard_timed(default_dispatcher(),stkbase,NULL,opts.pause_ms);
		}

		if(data_connection_count == 0) {
			client_dispatcher_timed(default_dispatcher(),stkbase,NULL,500);
			continue;
		}

		for(int cidx = 0; cidx < data_connection_count; cidx++)
			rc = stk_data_flow_send(data_connections[cidx],seq,flags);
		if(rc == STK_SUCCESS || rc == STK_WOULDBLOCK) {
			if(rc == STK_SUCCESS)
				seqs_sent++;

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
	gettimeofday(&sent_tv,NULL);

	{
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

	/* Let data drain before closing data flows */
	puts("Letting data flows drain");
	client_dispatcher_timed(default_dispatcher(),stkbase,NULL,5000);

	rc = stk_destroy_data_flow(df);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the data/notification data flow : %d",rc);

	rc = stk_destroy_env(stkbase);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	if(default_buffer) free(default_buffer);

	printf("%s ended: Sent %d sequences\n",argv[0],seqs_sent);
	if(blks > 0)
		printf("I/O blocked attempts: %d\n",blks);
	}
	return 0;
}

