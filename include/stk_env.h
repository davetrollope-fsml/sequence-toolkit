/** @file stk_env.h
 * This file provides definitions and typdefs etc required to interface
 * with the Sequence Toolkit APIs
 */
#ifndef STK_ENV_H
#define STK_ENV_H

/**
 * \typedef stk_env_t
 *
 * The stk_env_t is the handle used to manage Sequence Toolkit components.
 * Everything the Sequence Toolkit does requires access (directly or indirectly)
 * to a stk_env_t handle. Most APIs require one of these to be passed to it.
 *
 * \see stk_create_env() and stk_destroy_env()
 */
typedef struct stk_env_stct stk_env_t;

/**
 * Function pointer typedef to define a dispatcher wake up function.
 * This is called (if configured) when scheduling timers to ensure
 * that the application dispatcher can be influenced by timer events
 * The example dispatcher provided uses this to establish the next
 * time the loop should wake up.
 */
typedef void (*stk_wakeup_dispatcher_cb)(stk_env_t *);
#endif

