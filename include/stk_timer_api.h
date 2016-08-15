/** @file stk_timer_api.h
 * This file contains the implementation of a timer management subsystem.
 * It is based on a linked list whic could be improved performance wise
 * but allow multiple timer sets that can be used to offset the overhead
 * in insertions by reducing the number of timers per set.
 *
 * This timer API is not thread safe at this time
 */
#ifndef STK_TIMER_API_H
#define STK_TIMER_API_H
#include "stk_timer.h"
#include "stk_env.h"
#include "stk_common.h"

/**
 * Allocate a new timer set.
 *
 * The max_timers influences the means by which timers are managed for
 * optimal runtime performance. A value of 0 is valid and indicates
 * the timer set should be managed entirely dynamically, but of course,
 * this is a little slower but much more flexible. Time vs space vs complexity!
 *
 * \param env The environment which this timer set should be a part of
 * \param user_setdata A user pointer passed to each call back for this set
 * \param max_timers The maximum number of timers this set shall contain
 * \param add_to_pool Indicates if the timer should be added to the environment timer pools
 * \returns A handle to the allocated timer set
 * \see stk_free_timer_set() stk_env_dispatch_timer_pools()
 */
stk_timer_set_t *stk_new_timer_set(stk_env_t *env,void *user_setdata,stk_uint32 max_timers,stk_bool add_to_pool);
/**
 * Free a timer set.
 *
 * If you have data allocated per timer, use the expire_timers to force expiration
 * so you can free the memory.
 *
 * \param timer_set The timer set to be freed
 * \param cancel_timers Indicates if timers left in the set should be cancelled
 * \returns Whether the set was successfully freed
 * \see stk_new_timer_set()
 */
stk_ret stk_free_timer_set(stk_timer_set_t *timer_set,stk_bool cancel_timers);
/**
 * Schedule a timer callback for ms milliseconds from now.
 *
 * The timer callback will be called with STK_TIMER_EXPIRED as the cb_type
 *
 * IDs may be reused in the set
 *
 * \param timer_set The timer set to be used
 * \param cb The callback to be called when the timer has expired or cancelled
 * \param id A 64 bit unsigned user ID for this timer
 * \param userdata Applicstion data to be passed to callbacks when this timer expires
 * \param ms The number of milliseconds before the callback should bencalled
 * \returns A timer pointer that may be used to efficiently cancel the timer
 * \see stk_cancel_timer() stk_cancel_timer_id() stk_reschedule_timer() stk_timer_cb_type
 */
stk_timer_t * stk_schedule_timer(stk_timer_set_t *timer_set,stk_timer_cb cb,stk_uint64 id,void *userdata,long ms);
/**
 * Reschedule a previously scheduled timer (same ID, interval etc)
 * \param timer_set The timer set to be used
 * \param timer The timer (as returned by stk_schedule_timer() originally)
 * \returns Whether the timer was successfully rescheduled
 * \see stk_schedule_timer()
 */
stk_ret stk_reschedule_timer(stk_timer_set_t *timer_set,stk_timer_t *timer);
/**
 * Cancel a timer using the pointer returned when scheduling the timer
 *
 * The timer callback will be called with STK_TIMER_CANCELLED as the cb_type
 *
 * \param timer_set The timer set to be used
 * \param timer The timer to be cancelled
 * \returns Whether the timer was successfully cancelled
 * \see stk_schedule_timer() stk_timer_cb_type
 */
stk_ret stk_cancel_timer(stk_timer_set_t *timer_set,stk_timer_t *timer);
/**
 * Cancel a timer based on ID.
 *
 * Only the first instance of a timer with the ID will be cancelled.
 * If ID's are reused
 *
 * \returns Whether the timer was successfully cancelled or STK_NOT_FOUND
 * \see stk_schedule_timer() stk_timer_cb_type STK_NOT_FOUND
 */
stk_ret stk_cancel_timer_id(stk_timer_set_t *timer_set,stk_uint64 id);
/**
 * Dispatch the timers that have expired
 *
 * \param timer_set The timer set to be dispatched
 * \param max_callbacks The maximum number of callbacks to be executed (0 indicates any and all timers due, but is not recommended)
 * \returns Whether dispatching timers succeeded
 * \returns STK_SUCCESS if all timers due were expired
 * \returns STK_MAX_TIMERS if max_callbacks is met
 */
stk_ret stk_dispatch_timers(stk_timer_set_t *timer_set,unsigned short max_callbacks);
/**
 * Returns the number of milliseconds to the next timer expiration - useful for passing to poll() etc
 * \param timer_set The timer set being queried
 * \returns milliseconds until next timer expiration (or -1 if there is no timer)
 */
int stk_next_timer_ms(stk_timer_set_t *timer_set);
/**
 * Get the STK Environment from a timer set
 * \param timer_set The timer set from which the STK environ,ent is desired
 * \returns The environment which a timer set is associated with.
 */
stk_env_t *stk_env_from_timer_set(stk_timer_set_t *timer_set);


#endif
