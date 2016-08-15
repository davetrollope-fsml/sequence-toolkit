#include <stdio.h>
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_service_group_api.h"
#include "stk_test.h"

stk_ret stktest_service_group_cb(stk_service_group_t *svc_group, stk_service_t *svc, void *clientd)
{
	if(stk_get_service_state_in_group(svc_group,svc) != STK_SERVICE_IN_GROUP_JOINED)
		return !STK_SUCCESS;
	else
		return STK_SUCCESS;
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

	svcgrp = stk_create_service_group(stkbase,"create_service_group_test", 1000, NULL);

	{
	stk_service_t *svc,*svc2;

	svc = stk_create_service(stkbase,"create_service_group_test_service1", 0, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(svc!=NULL,"Failed to create a basic named data service object");

	{
	struct sockaddr_in ip;
	ip.sin_addr.s_addr = 0x7f000001;
	ip.sin_port = 10000;

	rc = stk_add_service_to_group(svcgrp,svc,ip,STK_SERVICE_IN_GROUP_EXPECTED);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to add first service to service group : %d",rc);
	}

	svc2 = stk_create_service(stkbase,"create_service_group_test_service2", 0, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(svc2!=NULL,"Failed to create a basic unnamed data service object");

	{
	struct sockaddr_in ip;
	ip.sin_addr.s_addr = 0x7f000001;
	ip.sin_port = 20000;

	rc = stk_add_service_to_group(svcgrp,svc2,ip,STK_SERVICE_IN_GROUP_JOINED);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to add second service to service group : %d",rc);
	}

	TEST_ASSERT(stk_get_service_state_in_group(svcgrp,svc)==STK_SERVICE_IN_GROUP_EXPECTED,"service 1 isnt report as in the expected state : %d",rc);


	/* The real content of this test - check each service state in the group and see if they are all joined */
	rc = stk_iterate_service_group(svcgrp,stktest_service_group_cb,NULL);
	TEST_ASSERT(rc!=STK_SUCCESS,"First iteration of service group should report the first service as not joined but everything was successful");

	rc = stk_set_service_state_in_group(svcgrp,svc,STK_SERVICE_IN_GROUP_JOINED);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to change state of service 1 : %d",rc);

	rc = stk_iterate_service_group(svcgrp,stktest_service_group_cb,NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Second iteration of service group should report all the services as joined but doesn't");

	rc = stk_set_service_group_state(svcgrp,svc,STK_SERVICE_GROUP_RUNNING);
	TEST_ASSERT(rc==STK_SUCCESS,"Setting service group state to running failed");


	/* Cleanup */
	rc = stk_remove_service_from_group(svcgrp,svc);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to remove first service from service group : %d",rc);

	rc = stk_remove_service_from_group(svcgrp,svc2);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to remove second service from service group : %d",rc);

	rc = stk_destroy_service(svc,NULL);                                                  
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the first service object : %d",rc);

	rc = stk_destroy_service(svc2,NULL);                                                  
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the second service object : %d",rc);

	}

	rc = stk_destroy_service_group(svcgrp);                                                  
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the service group object : %d",rc);

	rc = stk_destroy_env(stkbase);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);
	}

	printf("%s PASSED\n",argv[0]);
	return 0;
}

