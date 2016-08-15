#include <stdio.h>
#include "stk_env_api.h"
#include "stk_service_api.h"
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
	stk_service_t *svc;

	svc = stk_create_service(stkbase,"create_service_test", 0, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(svc!=NULL,"Failed to create a basic named data service object");

	rc = stk_destroy_service(svc,NULL);                                                  
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the basic named data service object : %d",rc);
	}

	{
	stk_service_t *svc;

	svc = stk_create_service(stkbase, NULL, 0, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(svc!=NULL,"Failed to create a basic unnamed data service object");

	rc = stk_destroy_service(svc,NULL);                                                  
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the basic unnamed data service object : %d",rc);
	}

	rc = stk_destroy_env(stkbase);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	printf("%s PASSED\n",argv[0]);
	return 0;
}

