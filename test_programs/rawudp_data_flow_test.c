#include <stdio.h>
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_sequence_api.h"
#include "stk_data_flow.h"
#include "stk_rawudp_api.h"
#include "stk_data_flow_api.h"
#include <poll.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "stk_test.h"

struct pollfd fdset[2];
int nfds = 0;

static unsigned char *default_buffer;
static int default_buffer_sz;

int test_mode; /* 0 - clients, 1 - server, 2 - multicast server */

stk_ret process_seq_segment(stk_sequence_t *seq, void *vdata, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	unsigned char *data = (unsigned char *) vdata;

	printf("Sequence %p Received %ld bytes of type %ld\n",seq,sz,user_type);

	switch(test_mode) {
	case 0:
		TEST_ASSERT(0,"should not be receiving data for senders (clients)");
		break;
	case 1:
		TEST_ASSERT(user_type == 0x44334433,"Received sequence doesn't have the right user type %lu",user_type);
		break;
	case 2:
		TEST_ASSERT(user_type == 0x33443344,"Received sequence doesn't have the right user type %lu",user_type);
		break;
	}

	if(data)
		printf("First bytes: %2x %2x %2x %2x\n",data[0],data[1],data[2],data[3]);

	return STK_SUCCESS;
}

void process_data(stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc;
	char *seq_name = stk_get_sequence_name(rcv_seq);

	switch(test_mode) {
	case 0:
		TEST_ASSERT(0,"should not be receiving data for senders (clients)");
		break;
	case 1:
		TEST_ASSERT(strcasecmp(seq_name,"UDP") == 0,"received sequence name doesn't match raw data default name");
		TEST_ASSERT(stk_get_sequence_id(rcv_seq) == 0x11221122,"Received sequence doesn't have the right ID %lu",stk_get_sequence_id(rcv_seq));
		TEST_ASSERT(stk_get_sequence_type(rcv_seq) == 99,"Received sequence doesn't have the right type %d",stk_get_sequence_type(rcv_seq));
		break;
	case 2:
		TEST_ASSERT(strcasecmp(seq_name,"multicast-server") == 0,"received sequence name doesn't match raw data default name");
		TEST_ASSERT(stk_get_sequence_id(rcv_seq) == 0x22112211,"Received sequence doesn't have the right ID %lu",stk_get_sequence_id(rcv_seq));
		TEST_ASSERT(stk_get_sequence_type(rcv_seq) == 88,"Received sequence doesn't have the right type %d",stk_get_sequence_type(rcv_seq));
		break;
	}
	TEST_ASSERT(stk_number_of_sequence_elements(rcv_seq) == 1,"Received sequence doesn't have one element %d",stk_number_of_sequence_elements(rcv_seq));

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

	fdset[0].fd = stk_rawudp_listener_fd(df);
	fdset[0].events = POLLIN;
	fdset[0].revents = 0;
	nfds = 1;

	rcv_seq = stk_create_sequence(stkbase,NULL,0,0,0,NULL);
	TEST_ASSERT(rcv_seq!=NULL,"Failed to allocate rcv test sequence");

	while(1) {
		fdset[0].revents = 0;
		do {
			rc = poll(fdset,nfds,10);
		} while(rc == -1 && errno == EINTR);
		TEST_ASSERT(rc>=0,"poll returned error %d %d",rc,errno);

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
	stk_sequence_t *seq;
	stk_data_flow_t *df;

	seq = stk_create_sequence(stkbase,"rawudp_data_flow_test1",0xfedcba98,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
	TEST_ASSERT(seq!=NULL,"Failed to allocate test sequence");

	if(argc > 1 && strcasecmp(argv[1],"server") == 0) {
		stk_options_t options[] = { { "bind_address", "127.0.0.1"}, {"bind_port", "29312"}, {"reuseaddr", NULL},
			{ "receive_buffer_size", "1024000" },
			/* Use default name for server, multicast server tests setting of name */ { "sequence_type", (void *) 99 },
			{ "sequence_id", (void *) 0x11221122 }, { "sequence_user_type", (void *) 0x44334433 },
			{ NULL, NULL } };

		if(argc > 2)
			default_buffer_sz = atoi(argv[2]);
		else
			default_buffer_sz = 500;
		default_buffer=malloc(default_buffer_sz);

		df = stk_rawudp_listener_create_data_flow(stkbase,"udp listener socket for data flow test",29190,options);
		TEST_ASSERT(df!=NULL,"Failed to create rawudp server data flow");

		test_mode = 1; /* set test mode so we know what values to expect */
		dispatch(df);

		rc = stk_destroy_data_flow(df);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the rawudp data flow: %d",rc);
	} else
	if(argc > 1 && strcasecmp(argv[1],"multicast-server") == 0) {
		stk_options_t options[] = { { "bind_address", "0.0.0.0"}, {"bind_port", "29312"}, {"reuseaddr", NULL},
			{ "receive_buffer_size", "1024000" }, { "multicast_address", "224.10.10.20" },
			{ "sequence_name", "multicast-server" }, { "sequence_type", (void *) 88 },
			{ "sequence_id", (void *) 0x22112211 }, { "sequence_user_type", (void *) 0x33443344 },
			{ NULL, NULL } };

		default_buffer_sz = 500;

		default_buffer=malloc(default_buffer_sz);

		df = stk_rawudp_listener_create_data_flow(stkbase,"udp listener socket for data flow test",29190,options);
		TEST_ASSERT(df!=NULL,"Failed to create rawudp server data flow");

		test_mode = 2; /* set test mode so we know what values to expect */

		dispatch(df);

		rc = stk_destroy_data_flow(df);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the rawudp data flow: %d",rc);
	} else {
		stk_generation_id gen_id;
		stk_options_t unicastoptions[] =   { { "destination_address", "127.0.0.1"}, {"destination_port", "29312"}, { "send_buffer_size", "512000" },{ NULL, NULL } };
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

		df = stk_rawudp_client_create_data_flow(stkbase,"rawudp client socket for data flow test",29090,options);
		TEST_ASSERT(df!=NULL,"Failed to create rawudp client data flow");

		/* Send some test data - a sequence with one segment */
		memset((char *) default_buffer,0x84,default_buffer_sz);
		default_buffer[0] = 0x80;
		default_buffer[1] = 0x81;

		gen_id = stk_get_sequence_generation(seq);

		rc = stk_add_reference_to_sequence(seq,default_buffer,default_buffer_sz,0x4e0);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to add reference data, size %d to sequence",default_buffer_sz);

		rc = stk_data_flow_send(df,seq,0);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to send reference data, size %d",default_buffer_sz);

		/* Check the sequence updated it generation */
		TEST_ASSERT(gen_id<stk_get_sequence_generation(seq),"After sending data, the sequence didn't have its generation updated");

		/* Now Send a multiple segment sequence by adding a second buffer half the size */
		rc = stk_add_reference_to_sequence(seq,default_buffer,default_buffer_sz/2,0x4e2);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to add reference data, size %d to sequence",default_buffer_sz/2);

		rc = stk_data_flow_send(df,seq,0);
		TEST_ASSERT(rc==STK_DATA_TOO_LARGE,"attempt to send data with two elements succeeded, which should not be allowed.");

		sleep(10);
	}
	free(default_buffer);

	rc = stk_destroy_sequence(seq);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the test sequence : %d",rc);

	rc = stk_destroy_data_flow(df);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the data flow : %d",rc);
	}

	{ /* Test parsing protocol strings for rawudp */
	stk_protocol_def_t pdef;
	stk_data_flow_parse_protocol_str(&pdef,"rawudp:1.2.3.4");
	TEST_ASSERT(strcmp(pdef.ip,"1.2.3.4") == 0,"Failed to parse basic rawudp IP: %s",pdef.ip);
	TEST_ASSERT(strcmp(pdef.port,"") == 0,"Failed to parse basic rawudp IP - port incorrect: %s",pdef.port);

	stk_data_flow_parse_protocol_str(&pdef,"rawudp:1.2.3.4:999");
	TEST_ASSERT(strcmp(pdef.ip,"1.2.3.4") == 0,"Failed to parse rawudp IP with port: %s",pdef.ip);
	TEST_ASSERT(strcmp(pdef.port,"999") == 0,"Failed to parse rawudp IP - port incorrect: %s should be 999",pdef.port);
	}

	rc = stk_destroy_env(stkbase);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	printf("%s PASSED\n",argv[0]);
	return 0;
}

