#include <stdio.h>
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_sequence_api.h"
#include "stk_data_flow.h"
#include "stk_tcp_server_api.h"
#include "stk_tcp_client_api.h"
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

stk_ret process_seq_segment(stk_sequence_t *seq, void *vdata, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	unsigned char *data = (unsigned char *) vdata;

	printf("Sequence %p Received %ld bytes of type %ld\n",seq,sz,user_type);

	if(data)
		printf("First bytes: %2x %2x %2x %2x\n",data[0],data[1],data[2],data[3]);

	return STK_SUCCESS;
}

void process_data(stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc;
	char *seq_name = stk_get_sequence_name(rcv_seq);

	TEST_ASSERT(strcasecmp(seq_name,"tcp_data_flow_test1") == 0,"received sequence name doesn't match sent data");

	TEST_ASSERT(stk_get_sequence_id(rcv_seq) == 0xfedcba98,"Received sequence doesn't have matching ID %lu",stk_get_sequence_id(rcv_seq));

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
	stk_data_flow_t *livechannel = NULL;
	stk_sequence_t *rcv_seq;
	stk_env_t *stkbase = stk_env_from_data_flow(df);
	int max_conns = 4;

	fdset[0].fd = stk_tcp_server_fd(df);
	fdset[0].events = POLLIN;
	fdset[0].revents = 0;
	nfds = 1;

	while(1) {
		fdset[0].revents = 0;
		fdset[1].revents = 0;
		do {
			rc = poll(fdset,nfds,10);
		} while(rc == -1 && errno == EINTR);
		TEST_ASSERT(rc>=0,"poll returned error %d %d",rc,errno);

		if(fdset[0].revents & POLLIN) {
			STK_LOG(STK_LOG_NORMAL,"Data on well known port");
			if(livechannel) {
				rc = stk_destroy_data_flow(livechannel);
				TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the live tcp data flow: %d",rc);
				STK_LOG(STK_LOG_NORMAL,"Live channel deleted");
			}
			livechannel = stk_tcp_server_accept(df);
			if(livechannel) {
				/* Allocate a sequence to receive data */
				rcv_seq = stk_create_sequence(stkbase,NULL,0,0,0,NULL);
				TEST_ASSERT(rcv_seq!=NULL,"Failed to allocate rcv test sequence");

				/* Preallocate buffer space */
				rc = stk_copy_to_sequence(rcv_seq,default_buffer,default_buffer_sz,0);
				TEST_ASSERT(rc==STK_SUCCESS,"Failed to preallocate buffer space to receive");

				fdset[1].fd = stk_tcp_server_fd(livechannel);
				fdset[1].events = POLLIN;
				fdset[1].revents = 0;
				nfds = 2;
				STK_LOG(STK_LOG_NORMAL,"Live channel added fd %d",fdset[1].fd);
			}
			max_conns--;
		}
		if(nfds > 1 && fdset[1].revents & POLLIN) {
			ssize_t len;
			stk_sequence_t *ret_seq;

			do {
				STK_LOG(STK_LOG_NORMAL,"Data on live channel");
				ret_seq = stk_data_flow_rcv(livechannel,rcv_seq,0);
				if(ret_seq == NULL) {
					stk_ret rc;

					rc = stk_destroy_data_flow(livechannel);
					TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the live tcp data flow: %d",rc);

					STK_LOG(STK_LOG_NORMAL,"Live channel deleted");
					nfds = 1;
					livechannel = NULL;

					/* free the data in the sequence */
					rc = stk_destroy_sequence(rcv_seq);
					TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the test sequence : %d",rc);

					if(max_conns== 0) break;
				}
				else
				{
					process_data(livechannel,ret_seq);
				}
			} while(livechannel && stk_data_flow_buffered(livechannel) == STK_SUCCESS);
		}
	}
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

	seq = stk_create_sequence(stkbase,"tcp_data_flow_test1",0xfedcba98,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
	TEST_ASSERT(seq!=NULL,"Failed to allocate test sequence");

	if(argc > 1 && strcasecmp(argv[1],"server") == 0) {
		stk_options_t options[] = { { "bind_address", "127.0.0.1"}, {"bind_port", "29312"}, {"reuseaddr", NULL}, {"nodelay", NULL},
		{ "receive_buffer_size", "1024000" }, { "send_buffer_size", "512000" },{ NULL, NULL } };

		if(argc > 2)
			default_buffer_sz = atoi(argv[2]);
		else
			default_buffer_sz = 500;
		default_buffer=malloc(default_buffer_sz);

		df = stk_tcp_server_create_data_flow(stkbase,"tcp server socket for data flow test",29090,options);
		TEST_ASSERT(df!=NULL,"Failed to create tcp server data flow");

		dispatch(df);

		rc = stk_destroy_data_flow(df);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the tcp data flow: %d",rc);
	}
	else
	{
		stk_generation_id gen_id;
		stk_options_t options[] = { { "connect_address", "127.0.0.1"}, {"connect_port", "29312"},
		{ "receive_buffer_size", "1024000" }, { "send_buffer_size", "512000" },{ NULL, NULL } };

		if(argc > 1)
			default_buffer_sz = atoi(argv[1]);
		else
			default_buffer_sz = 500;
		default_buffer = malloc(default_buffer_sz);

		df = stk_tcp_client_create_data_flow(stkbase,"tcp client socket for data flow test",29090,options);
		TEST_ASSERT(df!=NULL,"Failed to create tcp client data flow");

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
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to add reference data, size %d to sequence",default_buffer_sz);

		rc = stk_data_flow_send(df,seq,0);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to send reference data, size %d",default_buffer_sz);

		sleep(10);
	}
	free(default_buffer);

	rc = stk_destroy_sequence(seq);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the test sequence : %d",rc);

	rc = stk_destroy_data_flow(df);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the data flow : %d",rc);
	}

	{ /* Test parsing protocol strings for tcp */
	stk_protocol_def_t pdef;
	stk_data_flow_parse_protocol_str(&pdef,"tcp:1.2.3.4");
	TEST_ASSERT(strcmp(pdef.ip,"1.2.3.4") == 0,"Failed to parse basic tcp IP: %s",pdef.ip);
	TEST_ASSERT(strcmp(pdef.port,"") == 0,"Failed to parse basic tcp IP - port incorrect: %s",pdef.port);

	stk_data_flow_parse_protocol_str(&pdef,"tcp:1.2.3.4:999");
	TEST_ASSERT(strcmp(pdef.ip,"1.2.3.4") == 0,"Failed to parse tcp IP with port: %s",pdef.ip);
	TEST_ASSERT(strcmp(pdef.port,"999") == 0,"Failed to parse tcp IP - port incorrect: %s should be 999",pdef.port);

	stk_data_flow_parse_protocol_str(&pdef,"1.2.3.4");
	TEST_ASSERT(strcmp(pdef.protocol,"") == 0,"Failed to parse basic IP - protocol incorrect: %s",pdef.protocol);
	TEST_ASSERT(strcmp(pdef.ip,"1.2.3.4") == 0,"Failed to parse basic IP: %s",pdef.ip);
	TEST_ASSERT(strcmp(pdef.port,"") == 0,"Failed to parse basic IP - port incorrect: %s",pdef.port);

	stk_data_flow_parse_protocol_str(&pdef,"1.2.3.4:998");
	TEST_ASSERT(strcmp(pdef.protocol,"") == 0,"Failed to parse basic IP - protocol incorrect: %s",pdef.protocol);
	TEST_ASSERT(strcmp(pdef.ip,"1.2.3.4") == 0,"Failed to parse tcp IP with port: %s",pdef.ip);
	TEST_ASSERT(strcmp(pdef.port,"998") == 0,"Failed to parse tcp IP - port incorrect: %s should be 999",pdef.port);
	}

	rc = stk_destroy_env(stkbase);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	printf("%s PASSED\n",argv[0]);
	return 0;
}

