/** @file stk_sync_api.h
 * The stk_sync API's provide an atomic and mutex implementation used by STK modules
 */
#ifndef STK_SYNC_API_H
#define STK_SYNC_API_H
#include "stk_sync.h"
#include "stk_common.h"
#include <pthread.h>

#define STK_ATOMIC_INCR(_ptr) stk_fetch_and_incr_int((stk_int_t*)_ptr) /*!< Atomic Increment an integer */
#define STK_ATOMIC_DECR(_ptr) stk_fetch_and_decr_int((stk_int_t*)_ptr) /*!< Atomic Decrement an integer */
#define STK_ATOMIC_ADD(_ptr,_val) stk_fetch_and_add_int((stk_int_t*)_ptr,_val) /*!< Atomic add of a number to an integer */

/**
 * Create a non-recursive mutex
 * \returns STK_SUCCESS if created
 */
stk_ret stk_mutex_init(stk_mutex_t **mt_ptr);
/**
 * Destroy a mutex
 * \returns STK_SUCCESS if destroyed successfully
 */
stk_ret stk_mutex_destroy(stk_mutex_t *mutex);
/**
 * Lock a mutex
 * \returns STK_SUCCESS if locked successfully
 */
stk_ret stk_mutex_lock(stk_mutex_t *mutex);
/**
 * TryLock a mutex
 * \returns STK_SUCCESS if locked successfully
 */
stk_ret stk_mutex_trylock(stk_mutex_t *mutex);
/**
 * Unlock a mutex
 * \returns STK_SUCCESS if unlocked successfully
 */
stk_ret stk_mutex_unlock(stk_mutex_t *mutex);

/**
 * Atomic add of an int.
 * \returns The old value
 */
int stk_fetch_and_add( int * variable, int value );
/**
 * Atomic increment of an int.
 * \returns The old value
 */
#define stk_atomic_increment(_var) stk_fetch_and_add(_var,1)
/**
 * Atomic decrement of an int.
 * \returns The old value
 */
#define stk_atomic_decrement(_var) stk_fetch_and_add(_var,-1)
/**
 * Atomic read of an int.
 * \returns The old value
 */
#define stk_atomic_read(_var) stk_fetch_and_add(_var,0)

_stk_inline int stk_fetch_and_add_int(stk_int_t *ptr, int val);
_stk_inline int stk_fetch_and_incr_int(stk_int_t *ptr);
_stk_inline int stk_fetch_and_decr_int(stk_int_t *ptr);

#endif
