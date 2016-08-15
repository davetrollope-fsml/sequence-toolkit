/** @file stk_smartbeat_api.h
 * The stk_smartbeat module provides APIs that implement and control the 
 * aggregation of heartbeats (smartbeats), and their communication to 
 * other STK components
 */
#ifndef STK_SMARTBEAT_API_H
#define STK_SMARTBEAT_API_H
#include "stk_smartbeat.h"
#include "stk_service.h"
#include "stk_name_service.h"
#include "stk_env.h"

/**
 * Create a Smartbeat controller. The Smartbeat controller
 * is responsible for sending smart beats to other components.
 * \param env Environment this controller should use
 * \returns The controller that was created or NULL
 */
stk_smartbeat_ctrl_t *stk_create_smartbeat_ctrl(stk_env_t *env);

/**
 * Destroy a Smartbeat controller.
 * \param smb Smartbeat controller to be destroyed
 * \returns Whether the smartbeat was successfully destroyed
 */
stk_ret stk_destroy_smartbeat_ctrl(stk_smartbeat_ctrl_t *smb);

/**
 * Update the smartbeat with the current time
 * \param smb Smartbeat controller to be updated
 * \returns Whether the time update was successful
 */
stk_ret stk_smartbeat_update_current_time(stk_smartbeat_t *smb);

/**
 * Get the minimum time a smartbeat may occur.
 * \returns The number of milliseconds allowed for a smartbeat
 */
int stk_min_smartbeat_interval(stk_smartbeat_ctrl_t *smb);

/**
 * Add service to smartbeat controller for heartbeating
 * \param smb Smartbeat controller to be added to
 * \param svc Service to be added to the smartbeat controller
 * \returns Whether the service was successfully added 
 */
stk_ret stk_smartbeat_add_service(stk_smartbeat_ctrl_t *smb, stk_service_t *svc);

/**
 * Remove service from a smartbeat controller
 * \param smb Smartbeat controller to be updated
 * \param svc Service to be removed from the smartbeat controller
 * \returns Whether the service was successfully removed 
 */
stk_ret stk_smartbeat_remove_service(stk_smartbeat_ctrl_t *smb, stk_service_t *svc);

/**
 * Add a name service to smartbeat controller for heartbeating
 * \param smb Smartbeat controller to be added to
 * \param svc Service to be added to the smartbeat controller
 * \returns Whether the service was successfully added 
 */
stk_ret stk_smartbeat_add_name_service(stk_smartbeat_ctrl_t *smb, stk_name_service_t *svc);

/**
 * Remove a name service from a smartbeat controller
 * \param smb Smartbeat controller to be updated
 * \param svc Service to be removed from the smartbeat controller
 * \returns Whether the service was successfully removed 
 */
stk_ret stk_smartbeat_remove_name_service(stk_smartbeat_ctrl_t *smb, stk_name_service_t *svc);

/**
 * Determine if a smartbeat has past an interval
 * \param sb The smartbeat being tested
 * \param curr_time The current time (in smartbeat form)
 * \param ivl The interval to be checked
 * \returns STK_SUCCESS if the interval has expired
 */
stk_bool stk_has_smartbeat_timed_out(stk_smartbeat_t *sb,stk_smartbeat_t *curr_time,stk_uint32 ivl);
#endif
