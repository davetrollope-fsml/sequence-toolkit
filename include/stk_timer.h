/** @file stk_timer.h
 * This header provides the typedefs and definitions for the timer subsystem.
 */
#ifndef STK_TIMER_H
#define STK_TIMER_H

/**
 * \typedef stk_timer_set_t
 * The timer set consitutes a grouping of related timers. Applications
 * may choose to group timers as seen fit and may use as few as one set.
 */
typedef struct stk_timer_set_stct stk_timer_set_t;

/**
 * \typedef stk_timer_t
 * A generic handle to a scheduled timer>
 */
typedef void* stk_timer_t;

/**
 * \typedef stk_timer_cb_type
 * A callback type indicates what event the callback is for (cancelled/expired etc).
 * \see STK_TIMER_EXPIRED
 */
typedef int stk_timer_cb_type;

#define STK_TIMER_EXPIRED 1     /*!< Timer has Expired */
#define STK_TIMER_CANCELLED 2   /*!< Timer was Cancelled */

/** The callback signature to be used for timer callbacks */
typedef void (*stk_timer_cb)(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type);

#endif
