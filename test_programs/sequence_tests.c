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

	/* Basic Sequence Tests */
	{
	stk_sequence_t *seq;
	int test_data = 0xfe;

	seq = stk_create_sequence(stkbase, NULL, 0, STK_SEQUENCE_TYPE_DATA, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(seq!=NULL,"Failed to create a basic unnamed data sequence object");

	{ /* Check empty sequence handling */
	void *data;
	}

	/* Add some data, find it and remove it */
	rc = stk_add_reference_to_sequence(seq,&seq,sizeof(stk_sequence_t**),0x5e0);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to add reference data, size %ld to sequence",sizeof(stk_sequence_t**));

	{
	void *data;
	stk_uint64 sz;
	rc = stk_sequence_find_data_by_type(seq,0x5e0,&data,&sz);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to find reference data 0x5e0 rc %d",rc);
	TEST_ASSERT(data==&seq,"Reference found doesn't match reference added %p rc %d",&seq,rc);
	rc = stk_sequence_find_data_by_type(seq,0x4e0,&data,&sz);
	TEST_ASSERT(rc!=STK_SUCCESS,"Found non existant reference data 0x4e0 rc %d",rc);
	}

	{
	stk_uint64 removed = stk_remove_sequence_data_by_type(seq,0x5e0,1);
	TEST_ASSERT(removed==1,"Failed to remove reference data 0x5e0 rc %d",rc);
	}

	rc = stk_copy_to_sequence(seq,&test_data,sizeof(test_data), 0x5e1);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to copy data, size %ld to sequence",sizeof(test_data));

	{
	int *data;
	stk_uint64 sz;
	rc = stk_sequence_find_data_by_type(seq,0x5e1,(void **) &data,&sz);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to find copied data 0x5e1 rc %d",rc);
	TEST_ASSERT(*data==0xfe,"Data found %d doesn't match copied data added %p rc %d",*data,&seq,rc);
	}

	/* Now delete the sequence */
	rc = stk_destroy_sequence(seq);                                                  
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the basic unnamed data sequence object : %d",rc);
	}

	/* Merged sequence tests */
	{
	stk_sequence_t *seq1,*seq2;
	int test_data = 0xfd;

	seq1 = stk_create_sequence(stkbase, NULL, 0, STK_SEQUENCE_TYPE_DATA, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(seq1!=NULL,"Failed to create a mergable data sequence object");
	seq2 = stk_create_sequence(stkbase, NULL, 0, STK_SEQUENCE_TYPE_DATA, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(seq2!=NULL,"Failed to create a second mergable data sequence object");

	/* Add items to seq1 and seq2, count seq1, merge seq2, count seq1 */
	rc = stk_copy_to_sequence(seq1,&test_data,sizeof(test_data), 0x5e2);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to copy data, size %ld to sequence",sizeof(test_data));

	{
	int count = stk_number_of_sequence_elements(seq1);
	TEST_ASSERT(count == 1,"incorrect count %d of elements on seq1, should be 1",count);
	TEST_ASSERT(stk_has_any_sequence_elements(seq1) == 1,"has any doesn't think there are elements when there are");
	count = stk_number_of_sequence_elements(seq2);
	TEST_ASSERT(count == 0,"incorrect count %d of elements on seq2, should be 0",count);
	TEST_ASSERT(stk_has_any_sequence_elements(seq2) == 0,"has any thinks there are elements when there are none");
	}

	rc = stk_copy_to_sequence(seq2,&test_data,sizeof(test_data), 0x5e3);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to copy data, size %ld to sequence",sizeof(test_data));

	{
	int count = stk_number_of_sequence_elements(seq1);
	TEST_ASSERT(count == 1,"incorrect count %d of elements on seq1, should be 1",count);
	count = stk_number_of_sequence_elements(seq2);
	TEST_ASSERT(count == 1,"incorrect count %d of elements on seq2, should be 1",count);
	}

	rc = stk_add_sequence_reference_in_sequence(seq1,seq2, 0x5e4);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to merge sequences rc %d",rc);
	{
	int count = stk_number_of_sequence_elements(seq1);
	TEST_ASSERT(count == 2,"incorrect count %d of elements on seq1 after merge, should be 2",count);
	count = stk_number_of_sequence_elements(seq2);
	TEST_ASSERT(count == 1,"incorrect count %d of elements on seq2 after merge, should be 1",count);
	}

	/* Now delete the sequences */
	rc = stk_destroy_sequence(seq1);                                                  
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the basic unnamed data sequence object : %d",rc);
	rc = stk_destroy_sequence(seq2);                                                  
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the basic unnamed data sequence object : %d",rc);
	}

	/* Test holding of a sequence */
	{
	stk_sequence_t *seq = stk_create_sequence(stkbase, NULL, 0, STK_SEQUENCE_TYPE_DATA, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(seq!=NULL,"Failed to create a basic unnamed data sequence object");

	stk_hold_sequence(seq);

	/* Release the hold */
	rc = stk_destroy_sequence(seq);                                                  
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the basic unnamed data sequence object : %d",rc);

	/* Now delete the sequence */
	rc = stk_destroy_sequence(seq);                                                  
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the basic unnamed data sequence object : %d",rc);
	}

	/* Meta data tests */
	{
	int test_data = 0xfd;
	int *data = 0;
	stk_uint64 sz;
	stk_sequence_t *seq = stk_create_sequence(stkbase, NULL, 0, STK_SEQUENCE_TYPE_DATA, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(seq!=NULL,"Failed to create a basic unnamed data sequence object");

	rc = stk_copy_to_sequence_meta_data(seq,&test_data,sizeof(test_data), 0x600);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to copy data, size %ld to sequence meta data",sizeof(test_data));

	rc = stk_copy_to_sequence(seq,&test_data,sizeof(test_data), 0x601);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to copy data, size %ld to sequence meta data",sizeof(test_data));

	rc = stk_sequence_find_meta_data_by_type(seq,0x600,(void **) &data,&sz);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to find copied data 0x600 rc %d",rc);
	TEST_ASSERT(*data==0xfd,"Data found %d doesn't match copied meta data added %p rc %d",*data,&seq,rc);

	rc = stk_sequence_find_meta_data_by_type(seq,0x601,(void **) &data,&sz);
	TEST_ASSERT(rc!=STK_SUCCESS,"Found unexpected copied data 0x601 rc %d",rc);

	/* Now delete the sequence */
	rc = stk_destroy_sequence(seq);                                                  
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the basic unnamed data sequence object : %d",rc);
	}

	rc = stk_destroy_env(stkbase);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	printf("%s PASSED\n",argv[0]);
	return 0;
}

