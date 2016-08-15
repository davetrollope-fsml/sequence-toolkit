#include "stk_env_api.h"
#include "stk_timer_api.h"
#include "stk_test.h"
#include <stdio.h>
#include <unistd.h>

int expired;
int cancelled;
void *gluserdata;
void *glsetdata;

void timer_cb(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type)
{
	if(cb_type == STK_TIMER_EXPIRED)
		expired++;
	if(cb_type == STK_TIMER_CANCELLED)
		cancelled++;
	glsetdata = user_setdata;
	gluserdata = userdata;
}

void reset_cbdata()
{
	expired = 0;
	cancelled = 0;
	gluserdata = NULL;
	glsetdata = NULL;
}

int main(int argc,char *argv[])
{
	stk_env_t *env;
	stk_timer_set_t *tset;
	stk_ret rc;
	int timer_ivl = 100;

	{
	stk_options_t options[] = { { "inhibit_name_service", (void *)STK_TRUE}, { NULL, NULL } };

	env = stk_create_env(options);
	TEST_ASSERT(env!=NULL,"allocate an stk environment");
	}

	tset = stk_new_timer_set(env,(void*) 0x231,2,STK_FALSE);
	TEST_ASSERT(tset!=NULL,"Failed to allocate a timer set");

	do {
		/* basic timer tests */
		stk_timer_t t1 = stk_schedule_timer(tset,timer_cb,1,NULL,timer_ivl);
		reset_cbdata();
		rc = stk_dispatch_timers(tset,2);
		TEST_ASSERT(rc==STK_SUCCESS,"timer dispatch returned unexpectedly (%d)",rc);
		TEST_ASSERT(expired == 0,"expected 0 expiration on basic test, didn't get it");
		TEST_ASSERT(cancelled == 0,"expected 0 cancellations on basic test, didn't get it");
		TEST_ASSERT(glsetdata == NULL,"Didn't receive NULL set data on basic test");
		TEST_ASSERT(gluserdata == NULL,"Didn't receive NULL user data on basic test");
		reset_cbdata();
		rc = stk_cancel_timer(tset,t1);
		TEST_ASSERT(rc==STK_SUCCESS,"Failed to cancel timer (%d)",rc);
		TEST_ASSERT(expired == 0,"expected 0 expiration on basic test, didn't get it");
		TEST_ASSERT(cancelled == 1,"expected 0 cancellations on basic test, didn't get it");
		TEST_ASSERT(glsetdata == (void*)0x231,"Didn't receive expected set data on basic test");
		TEST_ASSERT(gluserdata == NULL,"Didn't receive NULL user data on basic test");

		reset_cbdata();
		t1 = stk_schedule_timer(tset,timer_cb,2,(void*) 0x8008,timer_ivl);
		sleep((timer_ivl*10)/1000);
		rc = stk_dispatch_timers(tset,2);
		TEST_ASSERT(rc==STK_SUCCESS,"timer dispatch returned unexpectedly (%d)",rc);
		TEST_ASSERT(expired == 1,"expected 1 expiration on basic test, didn't get it");
		TEST_ASSERT(cancelled == 0,"expected 0 cancellations on basic test, didn't get it");
		TEST_ASSERT(glsetdata == (void*)0x231,"Didn't receive expected set data on basic test");
		TEST_ASSERT(gluserdata == (void*) 0x8008,"Didn't receive expected user data on basic test");
		timer_ivl *= 15;
	} while(timer_ivl < 5000);

	{
	reset_cbdata();
	stk_timer_t t1 = stk_schedule_timer(tset,timer_cb,2,(void*) 0x8008,1000);
	TEST_ASSERT(stk_next_timer_ms(tset) > 900,"less than 900ms after scheduling timer! (%d)",rc);
	rc = stk_cancel_timer(tset,t1);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to cancel timer (%d)",rc);
	}

	rc = stk_free_timer_set(tset,0);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the timer set: %d",rc);

	rc = stk_destroy_env(env);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	printf("%s PASSED\n",argv[0]);
	return 0;
}

