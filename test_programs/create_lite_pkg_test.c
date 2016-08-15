#include <stdio.h>
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_test.h"

stk_service_t *svc[30];

int main(int argc,char *argv[])
{
	stk_env_t *stkbase;
	stk_bool rc;

	stkbase = stk_create_env(NULL);
	TEST_ASSERT(stkbase!=NULL,"Failed to allocate an stk environment");

	for(int i = 0; i < 30; i++) {
		svc[i] = stk_create_service(stkbase,"create_service_test", 0, STK_SERVICE_TYPE_DATA, NULL);
		if(i < 25)
			TEST_ASSERT(svc[i]!=NULL,"Failed to create a basic named data service object");
		else
			TEST_ASSERT(svc[i]==NULL,"Failed to limit basic named data service object creation");
	}

	for(int i = 0; i < 30; i++) {
		if(svc[i]) {
			rc = stk_destroy_service(svc[i],NULL);                                                  
			TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the basic named data service object : %d",rc);
		}
	}

	rc = stk_destroy_env(stkbase);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	printf("%s PASSED\n",argv[0]);
	return 0;
}

