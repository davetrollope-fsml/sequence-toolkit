#include <stdio.h>
#include "stk_env_api.h"
#include "stk_sequence_api.h"
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
	stk_sequence_t *seq;

	seq = stk_create_sequence(stkbase,"create_sequence_test", 0, STK_SEQUENCE_TYPE_DATA, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(seq!=NULL,"Failed to create a basic named data sequence object");

	rc = stk_destroy_sequence(seq);                                                  
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the basic named data sequence object : %d",rc);
	}

	{
	stk_sequence_t *seq;

	seq = stk_create_sequence(stkbase, NULL, 0, STK_SEQUENCE_TYPE_DATA, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(seq!=NULL,"Failed to create a basic unnamed data sequence object");

	/* Add some data and remove it */
	rc = stk_add_reference_to_sequence(seq,&seq,sizeof(stk_sequence_t**),0x5e0);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to add reference data, size %ld to sequence",sizeof(stk_sequence_t**));

	rc = stk_copy_to_sequence(seq,&seq,sizeof(stk_sequence_t**),0x5e1);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to add copy of data, size %ld to sequence",sizeof(stk_sequence_t**));

	{
	stk_uint64 removed;

	removed = stk_remove_sequence_data_by_type(seq, 0x5e0, 1);
	TEST_ASSERT(removed==1,"Failed to remove the user type 0x5e0 which was added to sequence %p",seq);

	removed = stk_remove_sequence_data_by_type(seq, 0x5e1, 1);
	TEST_ASSERT(removed==1,"Failed to remove the user type 0x5e0 which was added to sequence %p",seq);
	
	}

	/* Now delete the sequence */
	rc = stk_destroy_sequence(seq);                                                  
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the basic unnamed data sequence object : %d",rc);
	}

	rc = stk_destroy_env(stkbase);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	printf("%s PASSED\n",argv[0]);
	return 0;
}

