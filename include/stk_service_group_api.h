/** @file stk_service_group_api.h
 * The Service Group APIs define a module which enable services to be grouped together.
 * The module provides ways to add and remove services to a group and access services
 * in the group
 */
#ifndef STK_SERVICE_GROUP_API_H
#define STK_SERVICE_GROUP_API_H
#include "stk_service_group.h"
#include "stk_service.h"
#include "stk_env.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

/* The service group APIs provide a means to register services as groups.
 * It is possible for a service to be registered in multiple groups simultaneously
 */

/**
 * Create a Sequence Service Group.
 *
 * Accepts options: service_added_cb, service_removed_cb.
 * \see stk_options.txt
 * \returns A handle to the service group created
 */
stk_service_group_t *stk_create_service_group(stk_env_t *env,char *name, stk_service_group_id id, stk_options_t *options);
/**
 * Destroy a Service Group.
 * \returns Whether the service group was destroyed
 */
stk_ret stk_destroy_service_group(stk_service_group_t *svcgrp);

/**
 * Set the state of a Service Group.
 * \returns Whether the Service Group could be changed to the desired state 
 */
stk_ret stk_set_service_group_state(stk_service_group_t *svc_group,stk_service_t *svc,stk_service_group_state state);
/**
 * Get the state of a Service Group
 * \returns The Service Group state
 */
stk_service_group_state stk_get_service_group_state(stk_service_group_t *svc_group,stk_service_t *svc);

/**
 * Add a Service to a Service Group (setting the initial state for the service in the group)
 * \returns Whether the service could be added to the group
 */
stk_ret stk_add_service_to_group(stk_service_group_t *svc_group,stk_service_t *svc,struct sockaddr_in ip,stk_service_in_group_state state);
/**
 * Remove a Service from a Service Group
 * \returns Whether the service could be removed from the group
 */
stk_ret stk_remove_service_from_group(stk_service_group_t *svc_group,stk_service_t *svc);

/**
 * Set the state of a Service within a Service Group
 * \returns Whether the Service could be changed to the desired state in the Service Group
 */
stk_ret stk_set_service_state_in_group(stk_service_group_t *svc_group,stk_service_t *svc,stk_service_in_group_state state);
/**
 * Get the state of a Service within a Service Group
 * \returns The Service state in the Service Group
 */
stk_service_in_group_state stk_get_service_state_in_group(stk_service_group_t *svc_group,stk_service_t *svc);

/**
 * Find a Service by name in a Service Group
 * \returns A handle to the Service if found (or NULL)
 */
stk_service_t *stk_find_service_in_group_by_name(stk_service_group_t *svc_group,char *name,struct sockaddr_in ip);

/**
 * Find a Service by Service ID in a Service Group
 * \returns A handle to the Service if found (or NULL)
 */
stk_service_t *stk_find_service_in_group_by_id(stk_service_group_t *svc_group,stk_service_id id,struct sockaddr_in ip);

/**
 * API to execute a callback on each service in a service group.
 * It is common to use this to check the state of the services in order to determine a new group state
 * \returns STK_SUCCESS if all the services were iterated over (a callback returning failure halts iteration)
 */
stk_ret stk_iterate_service_group(stk_service_group_t *svc_group,stk_service_in_group_cb cb,void *clientd);

/**
 * Get the STK Environment from a Service Group
 * \returns A STK Environment handle
 */
stk_env_t *stk_get_service_group_env(stk_service_group_t *svcgrp);

/**
 * Get the name from a Service Group
 * \returns A reference pointer to the Service Group name
 */
char *stk_get_service_group_name(stk_service_group_t *svcgrp);

/**
 * Get the options for a service group
 * \returns Pointer to the options array for the service group
 */
stk_options_t *stk_get_service_group_options(stk_service_group_t *svcgrp);

/**
 * Handle a received smartbeat in a service group
 * \returns Success if the smartbeat was handled without error
 */
stk_ret stk_service_group_handle_smartbeat(stk_service_group_t *svc_group,stk_service_id svc_id,stk_smartbeat_t *smartbeat,struct sockaddr_in reporting_ip);

/**
 * Get the ID of a service group
 * \returns The service group ID
 */
stk_service_group_id stk_get_service_group_id(stk_service_group_t *svcgrp);
#endif
