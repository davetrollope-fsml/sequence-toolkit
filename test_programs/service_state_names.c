#include <stdio.h>
#include <string.h>
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_test.h"

int main(int argc,char *argv[])
{
	stk_env_t *stkbase;
	stk_ret rc;

	{
	stk_options_t options[] = { { "inhibit_name_service", (void *)STK_TRUE}, { NULL, NULL } };

	stkbase = stk_create_env(options);
	TEST_ASSERT(stkbase!=NULL,"allocate an stk environment");
	}

	{
	stk_service_t *svc1,*svc2;
	char state_name[STK_SERVICE_STATE_NAME_MAX];

	svc1 = stk_create_service(stkbase,"service 1 having state names tested", 0, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(svc1!=NULL,"Failed to create a basic named data service object");
	svc2 = stk_create_service(stkbase,"service 2 having state names tested", 0, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(svc2!=NULL,"Failed to create a basic named data service object");

	/* Get one of the default state names */
	stk_get_service_state_str(svc1,STK_SERVICE_STATE_STOPPED,state_name,STK_SERVICE_STATE_NAME_MAX);
	TEST_ASSERT(strcmp(state_name,"stopped")==0,"Failed to get the stopped state name from a service");

	stk_set_service_state_str(svc1,22,"twenty two",STK_SERVICE_STATE_NAME_MAX);
	stk_get_service_state_str(svc1,22,state_name,STK_SERVICE_STATE_NAME_MAX);
	TEST_ASSERT(strcmp(state_name,"twenty two")==0,"Failed to get the test state name from a service");

	/* Now try getting the same state from the second service to test for cross service contamination and we should get the int instead 
	 * so we implicitly test get ints back from a service ;-)
	 */
	stk_get_service_state_str(svc2,22,state_name,STK_SERVICE_STATE_NAME_MAX);
	TEST_ASSERT(strcmp(state_name,"22")==0,"Failed to get the int test state name from a service");

	rc = stk_destroy_service(svc1,NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy service 1: %d",rc);
	rc = stk_destroy_service(svc2,NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy service 2: %d",rc);
	}

	rc = stk_destroy_env(stkbase);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	printf("%s PASSED\n",argv[0]);
	return 0;
}

