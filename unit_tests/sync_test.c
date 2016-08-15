#include "stk_env_api.h"
#include "stk_sync_api.h"
#include "stk_sync.h"
#include "stk_unit_test.h"
#include <stdio.h>
#include <unistd.h>


int main(int argc,char *argv[])
{
	int num = 5;

	/* Test atomic operations */
	TEST_ASSERT(STK_ATOMIC_INCR(&num)==5,"should return 5 on first increment");
	TEST_ASSERT(num==6,"num should be 6 after first increment");
	TEST_ASSERT(STK_ATOMIC_DECR(&num)==6,"should return 6 on first decrement");
	TEST_ASSERT(num==5,"num should be 5 after first decrement");

	/* Test mutex */
	{
	stk_mutex_t *m;
	stk_ret rc = stk_mutex_init(&m);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to init the mutex %d",rc);
	rc = stk_mutex_lock(m);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to lock the mutex %d",rc);
	rc = stk_mutex_trylock(m);
	TEST_ASSERT(rc!=STK_SUCCESS,"locked a locked mutex!");
	rc = stk_mutex_unlock(m);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to unlock the mutex %d",rc);
	rc = stk_mutex_lock(m);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to relock the mutex %d",rc);
	rc = stk_mutex_unlock(m);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to unlock the relocked mutex %d",rc);
	rc = stk_mutex_destroy(m);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the relocked mutex %d",rc);
	}

	printf("PASSED");
	return 0;
}

