#include <stdio.h>
#include "stk_env_api.h"
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

	rc = stk_destroy_env(stkbase);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	printf("%s PASSED\n",argv[0]);
	return 0;
}

