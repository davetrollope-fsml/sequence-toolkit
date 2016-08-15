#include <stdio.h>
#include <string.h>
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_sequence_api.h"
#include "stk_service_group_api.h"
#include "stk_sg_automation_api.h"
#include "stk_test.h"
#include "../src/stk_df_internal.h"

stk_ret stk_add_test_client_ip(stk_sequence_t *seq)
{
	struct sockaddr test_addr;
	memset(&test_addr,0,sizeof(test_addr));

	return stk_copy_to_sequence_meta_data(seq,&test_addr,sizeof(test_addr),STK_DATA_FLOW_CLIENTIP_ID);
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
	stk_service_group_t *svcgrp;
	stk_sequence_t *seq;
	stk_service_t *svc;
	stk_service_t *fsvc;

	svcgrp = stk_create_service_group(stkbase,"service_group_auto_svc_test", 1000, NULL);
	TEST_ASSERT(svcgrp!=NULL,"Failed to create the service group");

	svc = stk_create_service(stkbase,"service to be added to sequence", 0, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(svc!=NULL,"Failed to create a basic named data service object");

	/* seq = stk_create_sequence(stkbase,"service_sequence_test1",0xfedcba98,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
	 * We should be able to add automation events to sequences without hardcoding the sequence ID - stk_sga_invoke should
	 * be drive by the operation type in the sequence - but until then...
	 */
	seq = stk_create_sequence(stkbase,"service_sequence_test1",STK_SERVICE_NOTIF_CREATE,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
	TEST_ASSERT(seq!=NULL,"Failed to allocate test sequence");

	rc = stk_sga_add_service_op_to_sequence(seq,svc,STK_SGA_CREATE_SVC);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to add service to sequence : %d",rc);

	rc = stk_sga_add_service_state_to_sequence(seq,svc,STK_SERVICE_STATE_RUNNING);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to add service state of RUNNING to sequence : %d",rc);

	rc = stk_add_test_client_ip(seq);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to add test client ip to sequence : %d",rc);

	/* invoke sequence on service group */
	rc = stk_sga_invoke(svcgrp,seq);
	TEST_ASSERT(rc==STK_SUCCESS,"Invocation of sequence %p to create source on service group %p failed %d",seq,svcgrp,rc);

	rc = stk_destroy_sequence(seq);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the test sequence : %d",rc);

	{
	struct sockaddr_in ip;
	ip.sin_addr.s_addr = 0;
	ip.sin_port = 0;

	fsvc = stk_find_service_in_group_by_name(svcgrp,"service to be added to sequence",ip);
	TEST_ASSERT(fsvc!=NULL,"Failed to find the service created through invocation : %d",rc);
	}

	seq = stk_create_sequence(stkbase,"service_sequence_test1",STK_SERVICE_NOTIF_DESTROY,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
	TEST_ASSERT(seq!=NULL,"Failed to allocate test sequence");

	rc = stk_sga_add_service_op_to_sequence(seq,fsvc,STK_SGA_DESTROY_SVC);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to add service to sequence : %d",rc);

	rc = stk_add_test_client_ip(seq);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to add test client ip to sequence : %d",rc);

	/* invoke sequence on service group */
	rc = stk_sga_invoke(svcgrp,seq);
	TEST_ASSERT(rc==STK_SUCCESS,"Invocation of sequence %p to destory service on service group %p failed %d",seq,svcgrp,rc);

	rc = stk_destroy_service_group(svcgrp);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the service group object : %d",rc);

	rc = stk_destroy_sequence(seq);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the sequence object : %d",rc);

	rc = stk_destroy_service(svc,NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the service object : %d",rc);
	}

	rc = stk_destroy_env(stkbase);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	printf("%s PASSED\n",argv[0]);
	return 0;
}

