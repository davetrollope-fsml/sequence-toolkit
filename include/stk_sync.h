/** @file stk_sync.h
 * This header provides the macros and type definitions for atomic and mutex
 * implementations used throughout STK.
 */
#ifndef STK_SYNC_H
#define STK_SYNC_H

#include <pthread.h>

#define _stk_inline

/** Definition of a mutex */
typedef pthread_mutex_t stk_mutex_t;
typedef struct { volatile int v;  } stk_int_t;


#endif
