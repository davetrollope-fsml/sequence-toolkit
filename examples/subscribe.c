/*
 * Copyright Dave Trollope 2013
 * This source code is not to be distributed without agreement from
 * D. Trollope
 *
 * This example demonstrates a basic subscriber which receives data
 * from a publisher. It supports TCP and UDP (Raw, Unicast and Multicast) data flows.
 *
 * It creates a name subscription on the name server to subscribe to name registrations
 * which contain the connection info required to listen to the data flow. The name subscription
 * is maintained so multiple publishers can be joined, or if they restart.
 *
 * The publisher registers the name and connectivity info.
 *
 * This example uses several macros defined in stk_examples.h to simplify understanding and
 * keep focus on the most important details.
 */
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_sequence_api.h"
#include "stk_data_flow.h"
#include "stk_tcp_client_api.h"
#include "stk_udp_client_api.h"
#include "stk_udp_listener_api.h"
#include "stk_rawudp_api.h"
#include "stk_data_flow_api.h"
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
 
/* command line options provided - set in process_cmdline() */
struct cmdopts {
	short quiet;
	char *multicast_ip;
	char *multicast_port;
	char *subscriber_name;
	/* See stk_examples.h */
	STK_NAME_SERVER_OPTS
	STK_BIND_OPTS
} opts;

/* Stats */
int seqs_rcvd;
int name_lookup_expired;
int name_lookup_cbs_rcvd;

int data_connection_count;
typedef struct subscriptions_stct {
	char *subscriber_ip;
	char *subscriber_port;
	char *subscriber_protocol;
	stk_data_flow_t *df;
} subscriptions_t;

subscriptions_t data_connections[5];
stk_data_flow_t *create_data_flow(stk_env_t *stkbase,char *ip, char *port, char *protocol);

int terminating;

void term(int signum)
{
	static int ending = 0;

	if(ending++) exit(5);

	printf("Received SIGTERM/SIGINT, exiting...\n");
	printf("Received: %d\n",seqs_rcvd);
	name_lookup_expired = 1;
	terminating = 1;
	stop_dispatching(default_dispatcher());
}

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

	if(!opts.quiet) {
		stk_sequence_type t = stk_get_sequence_type(rcv_seq);
		printf("data flow %p: Number of elements in received sequence: %d Sequence type: %s\n",rcvchannel,stk_number_of_sequence_elements(rcv_seq),STK_SEQ_TYPE_TO_STRING(t));
	}
	if(stk_get_sequence_type(rcv_seq) != STK_SEQUENCE_TYPE_DATA) return;

	/* Call process_seq_segment() on each element in the sequence */
	rc = stk_iterate_sequence(rcv_seq,process_seq_segment,NULL);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to process received sequence");

	seqs_rcvd++;
	if(!opts.quiet)
		printf("Received: %d\n",seqs_rcvd);
}

stk_ret meta_data_cb(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	if(!opts.quiet)
		printf("Meta data type %lu sz %lu\n",user_type,sz);
	return STK_SUCCESS;
}

void name_info_cb(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info,stk_name_info_cb_type cb_type)
{
	stk_env_t *env = (stk_env_t *) app_info;
	char ip[16];
	char *p;

	if(cb_type == STK_NS_REQUEST_EXPIRED) {
		name_lookup_cbs_rcvd++;
		name_lookup_expired = 1;
		terminating = 1;
		if(!opts.quiet)
			printf("Request expired on name %s, %d callbacks received\n",name_info->name,name_lookup_cbs_rcvd);
		return;
	}

	p = (char *) inet_ntop(AF_INET, &name_info->ip[0].sockaddr.sin_addr, ip, sizeof(ip));
	if(p) {
		char monitor_port[6];
		if(!opts.quiet)
			printf("Received info on name %s, IP %s Port %d Protocol %s\n",name_info->name,
				ip,name_info->ip[0].sockaddr.sin_port,name_info->ip[0].protocol);

		/* fd created callback will persist this data flow creation */
		create_data_flow(env, name_info->ip[0].ipstr, name_info->ip[0].portstr, name_info->ip[0].protocol);
	}

	if(name_info->meta_data) {
		stk_ret rc;

		if(!opts.quiet)
			printf("Name %s has %d meta data elements\n",name_info->name,stk_number_of_sequence_elements(name_info->meta_data));

		rc = stk_iterate_sequence(name_info->meta_data,meta_data_cb,NULL);
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
	fprintf(stderr,"Usage: subscribe [options] <name>\n");
	fprintf(stderr,"       -h                             : This help!\n");
	fprintf(stderr,"       -q                             : Quiet\n");
	fprintf(stderr,"       -B ip[:port]                   : IP and port to be bound (default: 0.0.0.0:29312)\n");
	fprintf(stderr,"       -R <[protocol:]ip[:port]>      : IP and port of name server\n");
	fprintf(stderr,"                                      : protocol may be <tcp|udp>\n");
}

int process_cmdline(int argc,char *argv[],struct cmdopts *opts )
{
	int rc;

	while(1) {
		rc = getopt(argc, argv, "hqB:R:");
		if(rc == -1) return 0;

		switch(rc) {
		case 'h': /* Help! */
			usage();
			exit(0);

		case 'R': /* Set the IP/Port of the Name Server */
			process_name_server_string(opts,optarg);
			break;

		case 'B': /* Set the IP/Port to bind to */
			process_bind_string(opts,optarg);
			break;

		case 'q': /* Be less verbose about whats happening */
			opts->quiet = 1;
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

void name_fd_destroyed_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int removed = dispatch_remove_fd(default_dispatcher(),fd);
	STK_ASSERT(removed != -1,"Failed to remove data flow (fd %d) from dispatcher",fd);
}

void destroy_data_flow(int idx)
{
	stk_data_flow_t *df = data_connections[idx].df;
	stk_ret rc;

	if(data_connections[idx].subscriber_ip)
		free(data_connections[idx].subscriber_ip);
	if(data_connections[idx].subscriber_port)
		free(data_connections[idx].subscriber_port);
	if(data_connections[idx].subscriber_protocol)
		free(data_connections[idx].subscriber_protocol);

	memset(&data_connections[idx],0,sizeof(data_connections[0]));

	rc = stk_destroy_data_flow(df);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the tcp data flow: %d",rc);
}

void data_fd_destroyed_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int shift = 0;
	for(int idx = 0;idx < data_connection_count;idx++) {
		if(shift)
			data_connections[idx - 1] = data_connections[idx];

		if(data_connections[idx].df == flow) {
			destroy_data_flow(idx);
			shift = 1;
		}
	}
	if(shift) data_connection_count--;

	if(!shift)
	{
		int removed = dispatch_remove_fd(default_dispatcher(),fd);
		STK_ASSERT(removed != -1,"Failed to remove data flow (fd %d) from dispatcher",fd);
	}
}

void name_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,fd_hangup_cb,process_name_responses);
	STK_ASSERT(added != -1,"Failed to add data flow (fd %d) to dispatcher",fd);
}

void data_fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,fd_hangup_cb,process_data);
	STK_ASSERT(added != -1,"Failed to add data flow (fd %d) to dispatcher",fd);
	printf("Adding df %p fd %d to dispatcher, count %d\n",flow,fd,data_connection_count);
}

stk_data_flow_t *create_data_flow(stk_env_t *stkbase,char *ip,char *port,char *protocol)
{
	stk_data_flow_t *df = NULL;

	if((!ip) || (!port) || (!protocol))
		return NULL;

	/* Look to see if this ip/port/protocol already exists and reuse if so */
	for(int idx = 0; idx < data_connection_count; idx++) {
		if(strcmp(data_connections[idx].subscriber_ip,ip) == 0 &&
		   strcmp(data_connections[idx].subscriber_port,port) == 0 &&
		   strcmp(data_connections[idx].subscriber_protocol,protocol) == 0)
		{
			printf("Returning cached df\n");
			return data_connections[idx].df;
		}
	}

	/*
	 * Create the server data flow (aka a listening socket)
	 */
	{
	int tcp = (strcmp(protocol,"tcp") == 0);
	int multicast = (strcmp(protocol,"multicast") == 0);
	int udp = (strcmp(protocol,"udp") == 0);
	int rawudp = (strcmp(protocol,"rawudp") == 0);

	if(multicast || udp || rawudp)
	{
		/* UDP Subscribers listen for data, create udp listening data flow */
		stk_options_t udp_options[] = { { "bind_address", "0.0.0.0"}, {"bind_port", "29312"}, { "reuseaddr", (void *) STK_TRUE },
			{ "receive_buffer_size", "16000000" },
			{ "fd_created_cb", (void *) data_fd_created_cb }, { "fd_destroyed_cb", (void *) data_fd_destroyed_cb },
			{ NULL, NULL /* placeholder for multicast */ },
			{ NULL, NULL } };


		if(multicast) {
			if(opts.bind_ip)
				udp_options[0].data = opts.bind_ip;

			udp_options[6].name = "multicast_address";
			udp_options[6].data = ip;
		}
		else
			udp_options[0].data = ip;

		udp_options[1].data = port;

		if(udp || multicast)
			df = stk_udp_create_subscriber(stkbase,"udp subscriber data flow", STK_EG_SERVER_DATA_FLOW_ID, udp_options);
		else
			df = stk_rawudp_create_subscriber(stkbase,"rawudp subscriber data flow", STK_EG_SERVER_DATA_FLOW_ID, udp_options);
		if(df == NULL) {
			printf("Failed to create udp/rawudp subscriber data flow");
			return NULL;
		}
	} else
	if(tcp) {
		/* TCP Subscribers connect to Publishers, create tcp client */
		stk_options_t tcp_options[] = {
			{ "destination_address", NULL}, {"destination_port", NULL}, { "nodelay", (void *) STK_TRUE},
			{ "receive_buffer_size", "16000000" },
			{ "fd_created_cb", (void *) data_fd_created_cb }, { "fd_destroyed_cb", (void *) data_fd_destroyed_cb }, { NULL, NULL }
			};

		if(ip) tcp_options[0].data = ip;
		if(port) tcp_options[1].data = port;

		df = stk_tcp_create_subscriber(stkbase,"tcp subscriber data flow", 29090, tcp_options);
		if(df == NULL) {
			printf("Failed to create tcp subscriber data flow");
			return NULL;
		}
	} else {
		printf("Unrecognized protocol %s\n",protocol);
		return NULL;
	}
	}

	printf("Adding df %p to data connection table, current size %d\n",df,data_connection_count);
	memset(&data_connections[data_connection_count],0,sizeof(data_connections[0]));
	data_connections[data_connection_count].subscriber_ip = strdup(ip);
	data_connections[data_connection_count].subscriber_port = strdup(port);
	data_connections[data_connection_count].subscriber_protocol = strdup(protocol);
	data_connections[data_connection_count].df = df;
	data_connection_count++;

	return df;
}

int main(int argc,char *argv[])
{
	stk_env_t *stkbase;
	stk_bool rc;
	int defbufsize = 500;

	signal(SIGTERM, term); /* kill */
	signal(SIGINT, term);  /* ctrl-c */
	sigignore(SIGPIPE); /* Some system benefit from ignoring SIGPIPE */

	/* Get the command line options and fill out opts with user choices */
	if(process_cmdline(argc,argv,&opts) == -1 || argc <= optind) {
		usage();
		exit(5);
	}

	opts.subscriber_name = argv[optind];
	printf("Subscribing to '%s'\n",opts.subscriber_name);

	/* Only log errors to stderr when running quiet */
	if(opts.quiet)
		stk_set_stderr_level(STK_LOG_ERROR);

	/*
	 * Create an STK environment. Since we are using the example listening dispatcher,
	 * set an option for the environment to ensure the dispatcher wakeup API is called.
	 */
	{
	stk_options_t name_server_data_flow_options[] = { { "destination_address", "127.0.0.1"}, {"destination_port", "20002"}, { "nodelay", (void *)STK_TRUE},
		{ "data_flow_name", "name server data flow for subscribe"}, { "data_flow_id", (void *) STK_NAME_SERVICE_DATA_FLOW_ID },
		{ "fd_created_cb", (void *) name_fd_created_cb }, { "fd_destroyed_cb", (void *) name_fd_destroyed_cb }, { NULL, NULL } };
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

	/* Lookup the name being subscribed to. Data flow will be created as part of processing a response */
	name_lookup_and_dispatch(stkbase, opts.subscriber_name, name_info_cb, name_lookup_expired, name_lookup_cbs_rcvd, 1);

	/* Wait for a connection */
	while(data_connection_count < 1 && !terminating)
		usleep(1000);

	/*
	 * Run the example dispatcher to process data from publishers. This example
	 * does this inline, but an application might choose to invoke this on another thread.
	 * 
	 * The dispatcher only returns when a shutdown is detected.
	 */
	printf("Dispatching\n");
	if(!terminating)
		eg_dispatcher(default_dispatcher(),stkbase,100);

	terminate_dispatcher(default_dispatcher());

	/* The dispatcher returned, destroy all the data flows, sequence and environment */
	for(int idx = 0; idx < data_connection_count; idx++)
		destroy_data_flow(idx);

	rc = stk_destroy_env(stkbase);
	STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	printf("%s terminated\n",argv[0]);
	return 0;
}

