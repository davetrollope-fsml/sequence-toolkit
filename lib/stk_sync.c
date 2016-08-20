#include "stk_sync_api.h"
#include "stk_internal.h"
#include <sys/time.h>
#include <errno.h>

stk_ret stk_mutex_init(stk_mutex_t **mt_ptr)
{
	int mt_create;

	*mt_ptr = malloc(sizeof(stk_mutex_t));
	if(*mt_ptr == NULL) return STK_MEMERR;

	mt_create = pthread_mutex_init(*mt_ptr,NULL);
	STK_CHECK(STKA_SYNC,mt_create==0,"pthread_mutex_init (rc %d)",mt_create);
	if(mt_create) {
		free(*mt_ptr);
		*mt_ptr = NULL;
		return STK_SYSERR;
	}

	return STK_SUCCESS;
}

stk_ret stk_mutex_destroy(stk_mutex_t *mutex)
{
	int mutex_error = pthread_mutex_destroy(mutex);
	if(mutex_error == EBUSY)
		STK_CHECK(STKA_SYNC,mutex_error==EBUSY,"no");

	STK_CHECK(STKA_SYNC,mutex_error==0,"Destroy of mutex %p (rc %d)",mutex,mutex_error);

	*((unsigned short *) mutex) = 0xfeaf;
	free(mutex);
	return STK_SUCCESS;
}

stk_ret stk_mutex_lock(stk_mutex_t *mutex)
{
	STK_DEBUG(STKA_SYNC,"Locking mutex %p",mutex);
{
	int mutex_error = pthread_mutex_lock(mutex);
	STK_CHECK(STKA_SYNC,mutex_error==0,"Lock of mutex %p (rc %d)",mutex,mutex_error);
	return mutex_error ? STK_SYSERR : STK_SUCCESS;
}
}

stk_ret stk_mutex_trylock(stk_mutex_t *mutex)
{
	int mutex_error = pthread_mutex_trylock(mutex);
	STK_CHECK(STKA_SYNC,mutex_error==0,"Trylock of mutex %p (rc %d)",mutex,mutex_error);
	return mutex_error == 0 ? STK_SUCCESS :
		   mutex_error == EBUSY ? STK_WOULDBLOCK : STK_SYSERR;
}

stk_ret stk_mutex_unlock(stk_mutex_t *mutex)
{
	int mutex_error = pthread_mutex_unlock(mutex);
	STK_CHECK(STKA_SYNC,mutex_error==0,"Unlock of mutex %p (rc %d)",mutex,mutex_error);
	return mutex_error ? STK_SYSERR : STK_SUCCESS;
}

_stk_inline int stk_fetch_and_add_int(stk_int_t *ptr, int val)
{
	__asm__ __volatile__ ("lock ; xadd %0,%1"
	: "=r" (val), "=m" (ptr->v)
	:  "0" (val),  "m" (ptr->v));
	return val;
}

pthread_mutex_t stk_atomic_lock;

_stk_inline int stk_fetch_and_incr_int(stk_int_t *ptr)
{
	int prev;
	pthread_mutex_lock(&stk_atomic_lock);
	prev = ptr->v;
	++(ptr->v);
	pthread_mutex_unlock(&stk_atomic_lock);
	return prev;
}

_stk_inline int stk_fetch_and_decr_int(stk_int_t *ptr)
{
	int prev;
	pthread_mutex_lock(&stk_atomic_lock);
	prev = ptr->v;
	--(ptr->v);
	pthread_mutex_unlock(&stk_atomic_lock);
	return prev;
}
