/** @file stk_env_api.h
 * The stk_env module provides APIs that tie together all Sequence Toolkit APIs.
 * Global level operations are implemented by the ENV object which is madantory
 * for the rest of the system. Everything exists within the domain of an STK
 * Environment object.
 */
#ifndef STK_ENV_API_H
#define STK_ENV_API_H

#include "stk_env.h"
#include "stk_common.h"
#include "stk_timer.h"
#include "stk_smartbeat.h"
#include "stk_name_service.h"
#include "stk_data_flow.h"

/**
 * \brief Create an STK environment.
 *
 * stk_create_env() creates a handle for the environment which controls
 * STK components. No STK component may live without an STK environment.
 *
 * \param options A pointer to a set of options to configure this environment
 * \returns A new stk_env_t handle
 * \see stk_options_t in stk_common.h
 */
stk_env_t *stk_create_env(stk_options_t *options);
/**
 * \brief Destroy(free) an STK environment.
 *
 * stk_destroy_env() destroys the STK handle passed to it and frees resources
 * assigned to it.
 */
stk_bool stk_destroy_env(stk_env_t *env);

/* Timer pool management */
/**
 * Add a timer set to the timer pool
 * \returns if there was space for a new timer set
 */
stk_ret stk_env_add_timer_set(stk_env_t *env,stk_timer_set_t *tset);
/**
 * Remove a timer set from the timer pool
 * \returns if the set was removed.
 */
stk_ret stk_env_remove_timer_set(stk_env_t *env,stk_timer_set_t *tset);
/**
 * Dispatch timers for all timer pools
 * \returns Whether all the timer sets were dispatched successfully
 */
stk_ret stk_env_dispatch_timer_pools(stk_env_t *env,unsigned short max_callbacks);
/**
 * Dispatch timers for a timer pool
 * \returns Whether all the timer sets were dispatched successfully
 */
stk_ret stk_env_dispatch_timer_pool(stk_env_t *env,unsigned short max_callbacks,int pool);
/**
 * Determine the interval to the next timer in the pool that will expire.
 * \returns The number of ms to the next timer expiration (or -1 if there are no timers).
 */
int stk_next_timer_ms_in_pool(stk_env_t *env);
/**
 * This function calls any registered dispatcher wakeup callback
 */
void stk_wakeup_dispatcher(stk_env_t *env);
/**
 * Get Smartbeat controller
 */
stk_smartbeat_ctrl_t *stk_env_get_smartbeat_ctrl(stk_env_t *env);
/**
 * Get the Name Service controller
 */
stk_name_service_t *stk_env_get_name_service(stk_env_t *env);
/**
 * Get the default Monitoring Data Flow 
 */
stk_data_flow_t *stk_env_get_monitoring_data_flow(stk_env_t *env);
/**
 * Get the default dispatcher
 */
void *stk_env_get_dispatcher(stk_env_t *env);

/**
 * Set the monitoring data flow for the STK environment
 */
stk_ret stk_set_env_monitoring_data_flow(stk_env_t *env,stk_options_t *options);

/**
 * Set the level for stderr logging
 */
void stk_set_stderr_level(int level);
#endif
