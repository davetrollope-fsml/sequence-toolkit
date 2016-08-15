#include <stdio.h>
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_sequence_api.h"
#include "stk_data_flow.h"
#include "stk_data_flow_api.h"
#include "stk_udp_listener_api.h"
#include "stk_udp_client_api.h"
#include "stk_data_flow_api.h"
#include <poll.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "stk_test.h"

#define DEFAULT_EXPIRATION_TIME 10

struct pollfd fdset[2];
int nfds = 0;

static unsigned char *default_buffer;
static int default_buffer_sz;
static int increment_sz = 25000;

#define BASE_SEQ_ID 0xfedcba80

int test_mode; /* 0 - clients, 1 - server, 2 - multicast-server */

stk_ret process_seq_segment(stk_sequence_t *seq, void *vdata, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	unsigned char *data = (unsigned char *) vdata;

	printf("Sequence %p Received %ld bytes of type %lx\n",seq,sz,user_type);

	switch(test_mode) {
	case 0:
		TEST_ASSERT(0,"should not be receiving data for senders (clients)");
		break;
	case 1:
	case 2:
		{
		int buffer_sz = default_buffer_sz - ((0xf - (user_type & 0xf)) * increment_sz);
		stk_generation_id gen_id = stk_get_sequence_generation(seq);

		switch(user_type & 0xff0) {
		case 0x4d0:
			TEST_ASSERT(sz == (stk_uint64) buffer_sz,"Received sequence doesn't have the right user type %lx and size %lu combination",user_type,sz);
			TEST_ASSERT(gen_id==1 || gen_id==2,"Generation ID of sequence with user_type %lx is wrong: %u",user_type,gen_id);
			break;
		case 0x4e0:
			TEST_ASSERT(sz == (stk_uint64) (buffer_sz/2),"Received sequence doesn't have the right user type %lx and size %lu combination",user_type,sz);
			TEST_ASSERT(gen_id==2,"Generation ID of sequence with user_type %lx is wrong: %u",user_type,gen_id);
			break;
		default:
			TEST_ASSERT(0,"Received user_type in segment sequence is unexpected %lx (size %lu)",user_type,sz);
			break;
		}
		}
	}

	if(data)
		printf("First bytes: %2x %2x %2x %2x\n",data[0],data[1],data[2],data[3]);

	return STK_SUCCESS;
}

void process_data(stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc;
	char *seq_name = stk_get_sequence_name(rcv_seq);

	TEST_ASSERT(seq_name,"received sequence doesn't have a name");

	TEST_ASSERT(strcasecmp(seq_name,"udp_data_flow_test1") == 0,"received sequence name doesn't match sent data");

	TEST_ASSERT((stk_get_sequence_id(rcv_seq) & BASE_SEQ_ID) == BASE_SEQ_ID,"Received sequence doesn't have matching ID %lu",stk_get_sequence_id(rcv_seq));

	TEST_ASSERT(stk_get_sequence_type(rcv_seq) == STK_SEQUENCE_TYPE_DATA,"Received sequence doesn't have data type %d",stk_get_sequence_type(rcv_seq));

	printf("Number of elements in received sequence %s: %d\n",seq_name ? seq_name : "", stk_number_of_sequence_elements(rcv_seq));

	{
	struct sockaddr_in from_address;
	socklen_t from_address_len;

	rc = stk_data_flow_client_ip(rcv_seq,&from_address,&from_address_len);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to find from address in received sequence rc %d",rc);
	printf("IP: %08x Port: %d\n",from_address.sin_addr.s_addr,from_address.sin_port);
	}

	/* Call process_seq_segment() on each element in the sequence */
	rc = stk_iterate_sequence(rcv_seq,process_seq_segment,NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to process received sequencebuffer space to receive");
}

void dispatch(stk_data_flow_t *df)
{
	int rc;
	stk_data_flow_t *livechannel = df;
	stk_sequence_t *rcv_seq;
	stk_env_t *stkbase = stk_env_from_data_flow(df);
	int max_conns = 4;
	int expiration_time = DEFAULT_EXPIRATION_TIME;

	fdset[0].fd = stk_udp_listener_fd(df);
	fdset[0].events = POLLIN;
	fdset[0].revents = 0;
	nfds = 1;

	rcv_seq = stk_create_sequence(stkbase,NULL,0,0,0,NULL);
	TEST_ASSERT(rcv_seq!=NULL,"Failed to allocate rcv test sequence");

	while(1) {
		/* Determine the time until the next timer will fire */
		expiration_time = stk_next_timer_ms_in_pool(stkbase);
		if(expiration_time == -1)
			expiration_time = DEFAULT_EXPIRATION_TIME;
		else
		{
			stk_ret ret = stk_env_dispatch_timer_pools(stkbase,0);
			TEST_ASSERT(ret == STK_SUCCESS,"Failed to dispatch timers: %d",ret);
		}

		fdset[0].revents = 0;
		do {
			rc = poll(fdset,nfds,expiration_time);
		} while(rc == -1 && errno == EINTR);
		TEST_ASSERT(rc >= 0,"poll returned error %d %d",rc,errno);

		if(fdset[0].revents & POLLIN) {
			ssize_t len;
			stk_sequence_t *ret_seq;

			do {
				STK_LOG(STK_LOG_NORMAL,"Data on live channel");

				ret_seq = stk_data_flow_rcv(livechannel,rcv_seq,0);
				if(ret_seq == NULL)
					break;

				process_data(livechannel,ret_seq);

			} while(stk_data_flow_buffered(livechannel) == STK_SUCCESS);
		}
	}

	/* free the data in the sequence */
	rc = stk_destroy_sequence(rcv_seq);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the test sequence : %d",rc);
}

int main(int argc,char *argv[])
{
	stk_env_t *stkbase;
	stk_bool rc;

	{
	stk_options_t options[] = { { "inhibit_name_service", (void *)STK_TRUE}, { NULL, NULL } };

	stkbase = stk_create_env(options);
	TEST_ASSERT(stkbase!=NULL,"allocate an stk environment");
	}

	{
	stk_data_flow_t *df;

	if(argc > 1 && strcasecmp(argv[1],"server") == 0) {
		stk_options_t options[] = { { "bind_address", "127.0.0.1"}, {"bind_port", "29312"}, {"reuseaddr", NULL},
			{ "receive_buffer_size", "1024000" }, { NULL, NULL } };

		if(argc > 2)
			default_buffer_sz = atoi(argv[2]);
		else
			default_buffer_sz = 500;
		default_buffer=malloc(default_buffer_sz);

		df = stk_udp_listener_create_data_flow(stkbase,"udp listener socket for data flow test",29190,options);
		TEST_ASSERT(df!=NULL,"Failed to create udp server data flow");

		test_mode = 1; /* set test mode so we know what values to expect */
		dispatch(df);

		rc = stk_destroy_data_flow(df);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the udp data flow: %d",rc);
	} else
	if(argc > 1 && strcasecmp(argv[1],"multicast-server") == 0) {
		stk_options_t options[] = { { "bind_address", "0.0.0.0"}, {"bind_port", "29312"}, {"reuseaddr", NULL},
			{ "receive_buffer_size", "1024000" }, { "multicast_address", "224.10.10.20" }, { NULL, NULL } };

		if(argc > 2)
			default_buffer_sz = atoi(argv[2]);
		else
			default_buffer_sz = 500;

		default_buffer=malloc(default_buffer_sz);

		df = stk_udp_listener_create_data_flow(stkbase,"udp listener socket for data flow test",29190,options);
		TEST_ASSERT(df!=NULL,"Failed to create udp server data flow");

		test_mode = 2; /* set test mode so we know what values to expect */

		dispatch(df);

		rc = stk_destroy_data_flow(df);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the udp data flow: %d",rc);
	} else {
		stk_sequence_t *seq;
		stk_generation_id gen_id;
		stk_options_t unicastoptions[] =   { { "destination_address", "127.0.0.1"}, {"destination_port", "29312"}, { "send_buffer_size", "512000" }, { NULL, NULL } };
		stk_options_t multicastoptions[] = { { "destination_address", "224.10.10.20"}, {"destination_port", "29312"}, { "send_buffer_size", "512000" }, { NULL, NULL } };
		stk_options_t *options;

		if(argc > 1 && strcasecmp(argv[1],"multicast") == 0) {
			options = multicastoptions;
			if(argc > 2)
				default_buffer_sz = atoi(argv[2]);
			else
				default_buffer_sz = 500;
		} else {
			options = unicastoptions;
			if(argc > 1)
				default_buffer_sz = atoi(argv[1]);
			else
				default_buffer_sz = 500;
		}
		default_buffer = malloc(default_buffer_sz);

		df = stk_udp_client_create_data_flow(stkbase,"udp client socket for data flow test",29090,options);
		TEST_ASSERT(df!=NULL,"Failed to create udp client data flow");

		/* Send some test data - a sequence with one segment */
		memset((char *) default_buffer,0x84,default_buffer_sz);
		default_buffer[0] = 0x80;
		default_buffer[1] = 0x81;

		{
		stk_uint64 user_type = 0x4df;
		stk_sequence_id seq_id = BASE_SEQ_ID;
		int seqs_sent = 0;

		for(int buffer_sz = default_buffer_sz; buffer_sz > 0; buffer_sz -= increment_sz) {
			seq = stk_create_sequence(stkbase,"udp_data_flow_test1",seq_id++,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
			TEST_ASSERT(seq!=NULL,"Failed to allocate test sequence");

			gen_id = stk_get_sequence_generation(seq);

			printf("sending sequence with buffer size %d type %lx\n",buffer_sz,user_type);

			rc = stk_add_reference_to_sequence(seq,default_buffer,buffer_sz,user_type);
			TEST_ASSERT(rc==STK_SUCCESS,"Failed to add reference data, size %d to sequence",buffer_sz);

			rc = stk_data_flow_send(df,seq,0);
			TEST_ASSERT(rc==STK_SUCCESS,"Failed to send reference data, size %d",buffer_sz);
			seqs_sent++;

			/* Check the sequence updated it generation */
			TEST_ASSERT(gen_id<stk_get_sequence_generation(seq),"After sending data, the sequence didn't have its generation updated");

			/* Now Send a multiple segment sequence by adding a second buffer half the size */
			printf("sending sequence with buffer sizes %d, %d (%d) types %lx %lx\n",buffer_sz,buffer_sz/2,buffer_sz+(buffer_sz/2),user_type,user_type + 0x10);

			rc = stk_add_reference_to_sequence(seq,default_buffer,buffer_sz/2,user_type + 0x10);
			TEST_ASSERT(rc==STK_SUCCESS,"Failed to add reference data, size %d to sequence",buffer_sz/2);

			rc = stk_data_flow_send(df,seq,0);
			TEST_ASSERT(rc==STK_SUCCESS,"send sequence with two elements");
			seqs_sent++;

			rc = stk_destroy_sequence(seq);
			TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the test sequence : %d",rc);

			user_type--;
			usleep(500000);
		}

		/* TODO: Send a sequence with 0 elements */
		{
			seq = stk_create_sequence(stkbase,"udp_data_flow_test1",seq_id++,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
			TEST_ASSERT(seq!=NULL,"Failed to allocate test sequence");

			gen_id = stk_get_sequence_generation(seq);

			printf("sending empty sequence with type %lx\n",user_type);

			rc = stk_data_flow_send(df,seq,0);
			TEST_ASSERT(rc==STK_SUCCESS,"Failed to send empty sequence");
			seqs_sent++;

			/* Check the sequence updated it generation */
			TEST_ASSERT(gen_id<stk_get_sequence_generation(seq),"After sending empty frag, the sequence didn't have its generation updated");
		}

		printf("sent %d sequences\n",seqs_sent);
		}
	}
	free(default_buffer);

	rc = stk_destroy_data_flow(df);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the data flow : %d",rc);
	}

	{ /* Test parsing protocol strings for udp and multicast */
	stk_protocol_def_t pdef;
	stk_data_flow_parse_protocol_str(&pdef,"udp:1.2.3.4");
	TEST_ASSERT(strcmp(pdef.ip,"1.2.3.4") == 0,"Failed to parse basic udp IP: %s",pdef.ip);
	TEST_ASSERT(strcmp(pdef.port,"") == 0,"Failed to parse basic udp IP - port incorrect: %s",pdef.port);

	stk_data_flow_parse_protocol_str(&pdef,"udp:1.2.3.4:999");
	TEST_ASSERT(strcmp(pdef.ip,"1.2.3.4") == 0,"Failed to parse udp IP with port: %s",pdef.ip);
	TEST_ASSERT(strcmp(pdef.port,"999") == 0,"Failed to parse udp IP - port incorrect: %s should be 999",pdef.port);

	stk_data_flow_parse_protocol_str(&pdef,"multicast:1.2.3.4");
	TEST_ASSERT(strcmp(pdef.ip,"1.2.3.4") == 0,"Failed to parse basic multicast IP: %s",pdef.ip);
	TEST_ASSERT(strcmp(pdef.port,"") == 0,"Failed to parse basic multicast IP - port incorrect: %s",pdef.port);

	stk_data_flow_parse_protocol_str(&pdef,"multicast:1.2.3.4:999");
	TEST_ASSERT(strcmp(pdef.ip,"1.2.3.4") == 0,"Failed to parse multicast IP with port: %s",pdef.ip);
	TEST_ASSERT(strcmp(pdef.port,"999") == 0,"Failed to parse multicast IP - port incorrect: %s should be 999",pdef.port);
	}

	rc = stk_destroy_env(stkbase);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	printf("%s PASSED\n",argv[0]);
	return 0;
}

