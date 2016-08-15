#include <stdio.h>
#include "stk_env_api.h"
#include "stk_sequence_api.h"
#include "stk_test.h"

stk_ret count_cb(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	int * count_ptr = (int *) clientd;
	*count_ptr = *count_ptr + 1;
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
	stk_sequence_t *seq;

	seq = stk_create_sequence(stkbase, NULL, 0, STK_SEQUENCE_TYPE_DATA, STK_SERVICE_TYPE_DATA, NULL);
	TEST_ASSERT(seq!=NULL,"Failed to create a basic unnamed data sequence object");

	{ /* Check empty sequence handling */
	void *data;
	stk_sequence_iterator_t *iter = stk_sequence_iterator(seq);
	TEST_ASSERT(iter!=NULL,"Failed to create sequnce iterator");

	data = stk_sequence_iterator_next(iter);
	TEST_ASSERT(data==NULL,"Should not get a value from stk_sequence_iterator_next() when empty");

	data = stk_sequence_iterator_prev(iter);
	TEST_ASSERT(data==NULL,"Should not get a value from stk_sequence_iterator_prev() when empty");

	rc = stk_end_sequence_iterator(iter);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to delete the sequence iterator for sequence %p",seq);
	}

	/* Add some data and remove it */
	rc = stk_add_reference_to_sequence(seq,&seq,sizeof(stk_sequence_t**),0x5e0);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to add reference data, size %ld to sequence",sizeof(stk_sequence_t**));

	{
	void *data;
	stk_sequence_iterator_t *iter = stk_sequence_iterator(seq);
	TEST_ASSERT(iter!=NULL,"Failed to create sequnce iterator");

	data = stk_sequence_iterator_data(iter);
	TEST_ASSERT(data==&seq,"Should get first value from stk_sequence_iterator_data() and it should be &seq");

	data = stk_sequence_iterator_next(iter);
	TEST_ASSERT(data==NULL,"Should not get a value from stk_sequence_iterator_next() at end of sequence");

	data = stk_sequence_iterator_prev(iter);
	TEST_ASSERT(data==&seq,"Should get last value from stk_sequence_iterator_prev() and it should be &seq");

	data = stk_sequence_iterator_prev(iter);
	TEST_ASSERT(data==NULL,"Should not get a value from stk_sequence_iterator_prev() at start of sequence");

	rc = stk_end_sequence_iterator(iter);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to delete the sequence iterator for sequence %p rc %d",seq,rc);
	}

	/* stk_iterate_sequence() test - count number of items */
	{
		int count = 0,test_data = 1;
		stk_sequence_t *seq2;

		rc = stk_iterate_sequence(seq,count_cb,&count);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to iterate over sequence with stk_iterate_sequence() sequence %p rc %d",seq,rc);
		TEST_ASSERT(count==1,"stk_iterate_sequence() called the count callback the wrong number of times %d expected 1",count);

		count = 0;
		rc = stk_iterate_complete_sequence(seq,count_cb,count_cb,count_cb,&count);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to iterate over sequence with stk_iterate_complete_sequence() sequence %p rc %d",seq,rc);
		TEST_ASSERT(count==3,"stk_iterate_sequence() called the count callback the wrong number of times %d expected 3",count);

		/* Add a merged sequence and make sure it is counted */
		seq2 = stk_create_sequence(stkbase, NULL, 0, STK_SEQUENCE_TYPE_DATA, STK_SERVICE_TYPE_DATA, NULL);
		TEST_ASSERT(seq2!=NULL,"Failed to create a second mergable data sequence object");

		rc = stk_add_sequence_reference_in_sequence(seq,seq2,0x5e4);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to merge sequences rc %d",rc);

		rc = stk_copy_to_sequence(seq2,&test_data,sizeof(test_data), 0x5e3);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to copy data, size %ld to sequence",sizeof(test_data));

		count = 0;
		rc = stk_iterate_sequence(seq,count_cb,&count);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to iterate over sequence with stk_iterate_sequence() sequence %p rc %d",seq,rc);
		TEST_ASSERT(count==2,"stk_iterate_sequence() called the count callback the wrong number of times %d expected 2",count);

		rc = stk_destroy_sequence(seq2);                                                  
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the merged data sequence object : %d",rc);
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

