#include "stk_timer_api.h"
#include "stk_internal.h"
#include "stk_env_api.h"
#include "stk_sync_api.h"
#include "PLists.h"

#include <sys/time.h>

/* Timer sets are distributes in to three pools based on their scheduled time 
 * This probably isn't the best algorithm or storage mechanism, but is workable.
 */
struct stk_timer_set_stct {
	stk_stct_type stct_type;
	stk_env_t *env;
	void *user_setdata;
	List *free_list;
	List *ms; /* Medium length timers */
	List *secs; /* Long timers */
	stk_uint32 max_timers;
	int flags;
	stk_mutex_t *timer_lock;
};

#define STK_TIMER_FLAG_ADDED_ENV 1

struct stk_timer_stct {
	stk_timer_cb cb;
	stk_uint64 id;
	void *userdata;
	struct timeval tv;
	long ms;
};

stk_timer_set_t *stk_new_timer_set(stk_env_t *env,void *user_setdata,stk_uint32 max_timers,stk_bool add_to_pool)
{
	stk_timer_set_t *timer_set;

	STK_CALLOC_STCT(STK_STCT_TIMER_SET,stk_timer_set_t,timer_set);
	if(timer_set) {
		timer_set->env = env;                        /* For future use, maybe memory pools, or global timer config */
		timer_set->user_setdata = user_setdata;
		timer_set->free_list = NewPList();
		timer_set->ms = NewPList();
		timer_set->secs = NewPList();
		if(max_timers > 0) {
			/* Preallocate timers */
			timer_set->max_timers = max_timers;
			for(unsigned int idx = 0; idx < max_timers; idx++) {
				Node *n = NewDataNode(sizeof(struct stk_timer_stct));
				STK_ASSERT(STKA_TIMER,n!=NULL,"preallocate timer");
				AddTail(timer_set->free_list,n);
			}
		}
		{
		stk_ret lockret = stk_mutex_init(&timer_set->timer_lock);
		STK_ASSERT(STKA_TIMER,lockret==STK_SUCCESS,"timer lock");
		}

		if(add_to_pool)
		{
			stk_ret rc;
			rc = stk_env_add_timer_set(env,timer_set);
			STK_ASSERT(STKA_MEM,rc==STK_SUCCESS,"add timer set %p to env %p", timer_set,env);
			timer_set->flags |= STK_TIMER_FLAG_ADDED_ENV;
		}
	}
	return timer_set;
}

stk_ret stk_free_timer_set(stk_timer_set_t *timer_set,stk_bool cancel_timers)
{
	stk_ret rc = STK_SUCCESS;

	STK_ASSERT(STKA_TIMER,timer_set->stct_type==STK_STCT_TIMER_SET,"destroy a timer set, the pointer was to a structure of type %d",timer_set->stct_type);

	while(!IsPListEmpty(timer_set->ms)) {
		Node *n = FirstNode(timer_set->ms);
		if(cancel_timers) {
			rc = stk_cancel_timer(timer_set,(stk_timer_t *) n); /* Puts on free list */
			STK_CHECK(STKA_TIMER,rc==STK_SUCCESS,"cancel timer in timer set %p rc %d",timer_set,rc);
		} else {
			Remove(n);
			FreeNode(n);
		}
	}
	while(!IsPListEmpty(timer_set->secs)) {
		Node *n = FirstNode(timer_set->secs);
		if(cancel_timers) {
			rc = stk_cancel_timer(timer_set,(stk_timer_t *) n); /* Puts on free list */
			STK_CHECK(STKA_TIMER,rc==STK_SUCCESS,"cancel timer in timer set %p rc %d",timer_set,rc);
		} else {
			Remove(n);
			FreeNode(n);
		}
	}

	/* must cancel before locking, because cancel locks! */
	rc = stk_mutex_lock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,rc==STK_SUCCESS,"lock timer set %p to destroy",timer_set);

	while(!IsPListEmpty(timer_set->free_list)) {
		Node *n = FirstNode(timer_set->free_list);
		Remove(n);
		FreeNode(n);
	}
	FreeList(timer_set->free_list);

	if(timer_set->flags & STK_TIMER_FLAG_ADDED_ENV) {
		rc = stk_env_remove_timer_set(timer_set->env,timer_set);
		STK_ASSERT(STKA_MEM,rc==STK_SUCCESS,"delete timer set %p from env %p",timer_set,timer_set->env);
	}

	rc = stk_mutex_unlock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,rc==STK_SUCCESS,"unlock timer set %p to destroy",timer_set);

	rc = stk_mutex_destroy(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,rc==STK_SUCCESS,"delete timer set %p lock from env %p",timer_set,timer_set->env);

	if(timer_set->ms) FreeList(timer_set->ms);
	if(timer_set->secs) FreeList(timer_set->secs);

	STK_FREE_STCT(STK_STCT_TIMER_SET,timer_set);
	return rc;
}

void stk_timer_insert(List *timer_list,Node *n,struct stk_timer_stct *t)
{
	if(IsPListEmpty(timer_list)) {
		AddTail(timer_list,n);
		return;
	}

	{
	Node *c = LastNode(timer_list);
	struct stk_timer_stct *ct = NodeData(c);
	while(timercmp(&ct->tv,&t->tv,>)) {
		if(IsFirstNode(c)) {
			c = PrvNode(c);
			break;
		}
		c = PrvNode(c);
		ct = (struct stk_timer_stct *) NodeData(c);
	}
	Insert(timer_list,n,c);
	}
}

stk_timer_t *stk_schedule_timer(stk_timer_set_t *timer_set,stk_timer_cb cb,stk_uint64 id,void *userdata,long ms)
{
	struct stk_timer_stct *t;
	Node *n;
	struct timeval tv;
	int rc = gettimeofday(&tv, NULL);
	if(rc != 0) return NULL;

	STK_ASSERT(STKA_TIMER,ms > 0,"stk_schedule_timer passed %ld <= 0 ms",ms);
	STK_ASSERT(STKA_TIMER,cb != NULL,"stk_schedule_timer passed null callback");
	tv.tv_usec += (ms*1000);
	tv.tv_sec += tv.tv_usec / 1000000;
	tv.tv_usec = tv.tv_usec % 1000000;

	{
	stk_ret ret = stk_mutex_lock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"lock timer set %p ret %d",timer_set,ret);

	if(!IsPListEmpty(timer_set->free_list))
	{
		n = FirstNode(timer_set->free_list);
		Remove(n);
	} else {
		n = NewDataNode(sizeof(struct stk_timer_stct));
		STK_CHECK(STKA_TIMER,n!=NULL,"allocated timer node");
		if(!n) {
			ret = stk_mutex_unlock(timer_set->timer_lock);
			STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"unlock timer set %p ret %d",timer_set,ret);
			return NULL;
		}
	}
	t = (struct stk_timer_stct *) NodeData(n);
	t->cb = cb;
	t->id = id;
	t->userdata = userdata;
	t->tv = tv;
	t->ms = ms;

	if(ms < 1000)
		stk_timer_insert(timer_set->ms,n,t);
	else
		stk_timer_insert(timer_set->secs,n,t);

	ret = stk_mutex_unlock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"unlock timer set %p ret %d",timer_set,ret);
	}

	/* Now wake up any dispatcher so it can determine if this timer shortens the time it should be a sleep */
	stk_wakeup_dispatcher(timer_set->env);

	return (stk_timer_t *) n;
}

stk_ret stk_reschedule_timer(stk_timer_set_t *timer_set,stk_timer_t *n)
{
	struct stk_timer_stct *t = (struct stk_timer_stct *) NodeData(n);
	int rc = gettimeofday(&t->tv, NULL);
	if(rc != 0) return STK_SYSERR;

	t->tv.tv_usec += (t->ms*1000);
	t->tv.tv_sec += t->tv.tv_usec / 1000000;
	t->tv.tv_usec = t->tv.tv_usec % 1000000;

	{
	stk_ret ret = stk_mutex_lock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"reschedule lock timer set %p timer %p ret %d",timer_set,n,ret);

	if(t->ms < 1000)
		stk_timer_insert(timer_set->ms,(Node*)n,t);
	else
		stk_timer_insert(timer_set->secs,(Node*)n,t);

	ret = stk_mutex_unlock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"unlock timer set %p ret %d",timer_set,ret);
	}

	return STK_SUCCESS;
}


stk_ret stk_cancel_timer_nolock(stk_timer_set_t *timer_set,stk_timer_t *timer)
{
	Node *n = (Node *) timer;
	struct stk_timer_stct *t;

	t = (struct stk_timer_stct *) NodeData(n);

	Remove(n);
	t->cb(timer_set,timer,t->id,t->userdata,timer_set->user_setdata,STK_TIMER_CANCELLED);

	AddHead(timer_set->free_list,(Node *) timer);

	return STK_SUCCESS;
}

stk_ret stk_cancel_timer(stk_timer_set_t *timer_set,stk_timer_t *timer)
{
	stk_ret rc;
	stk_ret ret = stk_mutex_lock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"cancel lock timer set %p ret %d",timer_set,ret);

	rc = stk_cancel_timer_nolock(timer_set,timer);

	ret = stk_mutex_unlock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"unlock timer set %p ret %d",timer_set,ret);
	return rc;
}


stk_ret stk_cancel_timer_id(stk_timer_set_t *timer_set,stk_uint64 id)
{
	stk_ret ret = stk_mutex_lock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"cancel id lock timer set %p ret %d",timer_set,ret);

	if(!IsPListEmpty(timer_set->ms))
		for(Node *n = FirstNode(timer_set->ms); !AtListEnd(n); n = NxtNode(n)) {
			struct stk_timer_stct *t = (struct stk_timer_stct *) NodeData(n);
			if(t->id == id) {
				stk_cancel_timer_nolock(timer_set,(stk_timer_t *)n);
				ret = stk_mutex_unlock(timer_set->timer_lock);
				STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"unlock timer set %p ret %d",timer_set,ret);
				return STK_SUCCESS;
			}
		}
	if(!IsPListEmpty(timer_set->secs))
		for(Node *n = FirstNode(timer_set->secs); !AtListEnd(n); n = NxtNode(n)) {
			struct stk_timer_stct *t = (struct stk_timer_stct *) NodeData(n);
			if(t->id == id) {
				stk_cancel_timer_nolock(timer_set,(stk_timer_t *)n);
				ret = stk_mutex_unlock(timer_set->timer_lock);
				STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"unlock timer set %p ret %d",timer_set,ret);
				return STK_SUCCESS;
			}
		}

	ret = stk_mutex_unlock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"unlock timer set %p ret %d",timer_set,ret);

	return STK_NOT_FOUND;
}

stk_ret stk_dispatch_timers(stk_timer_set_t *timer_set,unsigned short max_callbacks)
{
	unsigned short cbs = 0;
	int found = 0;
	struct timeval tv;
	int rc = gettimeofday(&tv, NULL);
	if(rc != 0) return !STK_SUCCESS;

#if 0
Locking while dispatching prevents scheduling/cancelling from callbacks.... Need to resolve
	stk_ret ret = stk_mutex_lock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"dispatch lock timer set %p ret %d",timer_set,ret);
#endif

	do {
		found = 0;
		if(!IsPListEmpty(timer_set->ms))
			for(Node *n = FirstNode(timer_set->ms); !AtListEnd(n) && (max_callbacks ? cbs < max_callbacks : 1);) {
				struct stk_timer_stct *t = (struct stk_timer_stct *) NodeData(n);
				if(timercmp(&tv,&t->tv,>) || timercmp(&tv,&t->tv,==)) {
					Node *n2 = NxtNode(n);
					Remove(n);
					t->cb(timer_set,(stk_timer_t*)n,t->id,t->userdata,timer_set->user_setdata,STK_TIMER_EXPIRED);
					/* If a callback calls stk_reschedule_timer(), the timer will be linked in to a timer list.
					 * So, only add it to the free list if it is still unlinked.
					 */
					if(!IsLinked(n))
						AddHead(timer_set->free_list,n);
					cbs++;
					found = 1;
					n = n2;
				}
				else
					break;
			}
		if(!IsPListEmpty(timer_set->secs))
			for(Node *n = FirstNode(timer_set->secs); !AtListEnd(n) && (max_callbacks ? cbs < max_callbacks : 1);) {
				struct stk_timer_stct *t = (struct stk_timer_stct *) NodeData(n);
				if(timercmp(&tv,&t->tv,>) || timercmp(&tv,&t->tv,==)) {
					Node *n2 = NxtNode(n);
					Remove(n);
					t->cb(timer_set,(stk_timer_t*)n,t->id,t->userdata,timer_set->user_setdata,STK_TIMER_EXPIRED);
					/* If a callback calls stk_reschedule_timer(), the timer will be linked in to a timer list.
					 * So, only add it to the free list if it is still unlinked.
					 */
					if(!IsLinked(n))
						AddHead(timer_set->free_list,n);
					cbs++;
					found = 1;
					n = n2;
				}
				else
					break;
			}
	} while(found && (max_callbacks ? cbs < max_callbacks : 1));
	if(max_callbacks > 0 && cbs == max_callbacks) return STK_MAX_TIMERS;

#if 0
	ret = stk_mutex_unlock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"unlock timer set %p ret %d",timer_set,ret);
#endif

	return STK_SUCCESS;
}

int stk_next_timer_ms(stk_timer_set_t *timer_set)
{
	int ms = 0;
	struct timeval curr_time;
	int rc = gettimeofday(&curr_time, NULL);
	if(rc != 0) return 0;

	stk_ret ret = stk_mutex_lock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"next lock timer set %p ret %d",timer_set,ret);

	if(!IsPListEmpty(timer_set->ms)) {
		struct stk_timer_stct *t = (struct stk_timer_stct *) NodeData(FirstNode(timer_set->ms));

		if(timercmp(&curr_time,&t->tv,>)) {
			ret = stk_mutex_unlock(timer_set->timer_lock);
			STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"unlock timer set %p ret %d",timer_set,ret);

			return 0;
		}

		ms += (t->tv.tv_sec - curr_time.tv_sec) * 1000;
		if(curr_time.tv_usec > t->tv.tv_usec) {
			ms -= 1000;
			ms += ((1000000 - curr_time.tv_usec)/1000) + (t->tv.tv_usec/1000);
		} else
			ms += (t->tv.tv_usec - curr_time.tv_usec)/ 1000;

		ret = stk_mutex_unlock(timer_set->timer_lock);
		STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"unlock timer set %p ret %d",timer_set,ret);

		return ms > 0 ? ms : 1; /* If we rounded down to 0, lets assume a 1ms delay */
	}
	else
	if(!IsPListEmpty(timer_set->secs)) {
		struct stk_timer_stct *t = (struct stk_timer_stct *) NodeData(FirstNode(timer_set->secs));

		if(timercmp(&curr_time,&t->tv,>)) {
			ret = stk_mutex_unlock(timer_set->timer_lock);
			STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"unlock timer set %p ret %d",timer_set,ret);

			return 0;
		}

		ms += (t->tv.tv_sec - curr_time.tv_sec) * 1000;
		if(curr_time.tv_usec > t->tv.tv_usec) {
			ms -= 1000;
			ms += ((1000000 - curr_time.tv_usec)/1000) + (t->tv.tv_usec/1000);
		} else
			ms += (t->tv.tv_usec - curr_time.tv_usec)/ 1000;

		ret = stk_mutex_unlock(timer_set->timer_lock);
		STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"unlock timer set %p ret %d",timer_set,ret);

		return ms > 0 ? ms : 1; /* If we rounded down to 0, lets assume a 1ms delay */
	}

	ret = stk_mutex_unlock(timer_set->timer_lock);
	STK_ASSERT(STKA_TIMER,ret==STK_SUCCESS,"unlock timer set %p ret %d",timer_set,ret);

	return -1;
}

stk_env_t *stk_env_from_timer_set(stk_timer_set_t *tset)
{
	STK_ASSERT(STKA_TIMER,tset!=NULL,"timer set null or invalid :%p",tset);
	STK_ASSERT(STKA_TIMER,tset->stct_type==STK_STCT_TIMER_SET,"timer set %p passed in to stk_env_from_timer_set is structure type %d",tset,tset->stct_type);
	return tset->env;
}

