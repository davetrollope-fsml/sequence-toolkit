/** @file stk_service_api.h
 * The Service API defines the mechanism by which services can comme in to existance and
 * subsequently be destroyed.
 */
#ifndef STK_SERVICE_API_H
#define STK_SERVICE_API_H

#include "stk_env.h"
#include "stk_service.h"
#include "stk_sequence.h"
#include "stk_data_flow.h"
#include "stk_common.h"

/**
 * Create a Sequence Service
 * \returns A handle to the service or NULL
 */
stk_service_t *stk_create_service(stk_env_t *env,char *name, stk_service_id id, stk_service_type type, stk_options_t *options);

/**
 * Destroy a Sequence Service
 * \param svc The service to be destroyed
 * \param last_state An optional pointer to the last state to be reported for this service. If NULL, STOPPED will be used as the last state
 * \returns Whether the service was successfully destroyed
 */
stk_ret stk_destroy_service(stk_service_t *svc,stk_service_state *last_state);

/* APIs to manage service state */
/**
 * Set the state of a Service
 * \returns Whether the Service could be changed to the desired state 
 */
stk_ret stk_set_service_state(stk_service_t *svc,stk_service_state state);
/**
 * Get the state of a Service
 * \returns The Service state
 */
stk_service_state stk_get_service_state(stk_service_t *svc);
/**
 * Get the activity timeout for this service
 * \returns The number of milliseconds before the service is determined inactive
 */
stk_uint32 stk_get_service_activity_tmo(stk_service_t *svc);

/* APIs to add or remove metadata from a service */
#if 0
/**
 * Add data in a Sequence to a service as metadata
 * [Not yet implemented]
 * \returns Whether the data could be added
 */
stk_ret stk_service_add_metadata(stk_service_t *svc,stk_sequence_t *metadata);
/**
 * Delete data in a Sequence from a services metadata
 * [Not yet implemented]
 * \returns Whether the data could be deleted
 */
stk_ret stk_service_del_metadata(stk_service_t *svc,stk_sequence_t *metadata);
#endif
/**
 * Get the name of a Service
 * \returns A reference pointer to the Service name
 */
char *stk_get_service_name(stk_service_t *svc);
/**
 * Get the Service ID
 * \returns The ID of the service
 */
stk_service_id stk_get_service_id(stk_service_t *svc);
/**
 * Get the type of a Service
 * \returns The Service type
 */
stk_service_type stk_get_service_type(stk_service_t *svc);

/* Heartbeat and application liveness APIs */
#include "stk_smartbeat.h"

#if 0
/**
 * The stk_service_bump_smartbeat_checkpoint() function updates by one the last checkpoint.
 * Applications should call this API routinely to indicate progress
 */
stk_ret stk_service_bump_smartbeat_checkpoint(stk_service_t *svc);

/**
 * The stk_service_last_smartbeat() function fills out the last heartbeat and checkpoint
 * received by a service.
 */
stk_ret stk_service_last_smartbeat(stk_service_t *svc,stk_smartbeat_t *smartbeat);

/**
 * The stk_service_update_smartbeat_time() function updates the last heartbeat time
 */
stk_ret stk_service_update_smartbeat_time(stk_service_t *svc,struct timeval *smartbeat);

#endif

/**
 * The stk_service_update_smartbeat() function updates the services smartbeat with
 * the value passed in
 *
 */
void stk_service_update_smartbeat(stk_service_t *svc,stk_smartbeat_t *smartbeat);

/**
 * The stk_service_update_smartbeat_checkpoint() function updates the last checkpoint.
 */
void stk_service_update_smartbeat_checkpoint(stk_service_t *svc,stk_checkpoint_t checkpoint);

/**
 * Get array of flows that need to be sent to for heartbeats.
 * Array ends with NULL
 * Caller must free returned data with free()
 */
stk_data_flow_t **stk_svc_get_smartbeat_flows(stk_service_t *svc);
/**
 * Get a copy of the service's current smartbeat
 */
void stk_get_service_smartbeat(stk_service_t *svc,stk_smartbeat_t *smb);
/**
 * Get the services monitoring df
 */
stk_data_flow_t *stk_get_monitoring_df(stk_service_t *svc);
/**
 * Get the services notification df
 */
stk_data_flow_t *stk_get_notification_df(stk_service_t *svc);
/**
 * Get the STK env from a service
 */
stk_env_t *stk_env_from_service(stk_service_t *svc);
/**
 * Get a copy of the string associated with a state for a service.
 * Will return an int (decimal) in string form if there is no known name.
 * \see stk_set_service_state_str
 */
void stk_get_service_state_str(stk_service_t *svc,stk_service_state state,char *state_str,size_t size);
/**
 * Add a state name to the state table for a service. This will replace any existing entry.
 * It is the application responsibility to track any dynamic memory used to store names
 * (and detect when they are replaced).
 * \see stk_get_service_state_str
 */
void stk_set_service_state_str(stk_service_t *svc,stk_service_state state,char *state_str,size_t size);

/**
 * Get the options for a service
 * \returns Pointer to the options array for the service
 */
stk_options_t *stk_get_service_options(stk_service_t *svc);
#endif
