#include <stdio.h>
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_service_group_api.h"
#include "stk_test.h"


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
	stk_service_t *svc,*svc2,*svcq;

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

	rc = stk_set_service_state_in_group(svcgrp,svc,STK_SERVICE_IN_GROUP_JOINED);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to change state of service 1 : %d",rc);

	TEST_ASSERT(stk_get_service_state_in_group(svcgrp,svc)==STK_SERVICE_IN_GROUP_JOINED,"service 1 isnt report as in the joined state : %d",rc);

	{
	struct sockaddr_in ip;
	ip.sin_addr.s_addr = 0x7f000001;
	ip.sin_port = 10000;

	svcq = stk_find_service_in_group_by_name(svcgrp,"create_service_group_test_service1",ip);
	TEST_ASSERT(svcq==svc,"After searching for service 1 by name, it wasn't found, returned %p",svcq);
	}

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

