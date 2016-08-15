/** @file stk_service_group.h
 * The stk_service_group_api.h file provides the typedefs and definitions
 * required to interface with the Server Group APIs
 */
#ifndef STK_SERVICE_GROUP_H
#define STK_SERVICE_GROUP_H
#include "stk_common.h"
#include "stk_service.h"
#include "stk_smartbeat.h"

/**
 * \typedef stk_service_group_t
 * A handle used to access a Sequence Service Group
 * \see stk_create_service_group()
 */
typedef struct stk_service_group_stct stk_service_group_t;

/**
 * \typedef stk_service_group_id
 * A 64 bit unsigned value which represents the Service Group ID.
 * Applications may define their own group IDs
 */
typedef stk_uint64 stk_service_group_id;

/**
 * \typedef stk_service_group_state
 * A 16 bit unsigned value which represents the state of a service group.
 * Applications may define their own states, but several are mandatory. 
 * \see STK_SERVICE_GROUP_INIT, STK_SERVICE_GROUP_RUNNING etc and stk_set_service_group_state()
 */
typedef stk_uint16 stk_service_group_state;

#define STK_SERVICE_GROUP_INIT 1     /*!< Service Group is Initialising */
#define STK_SERVICE_GROUP_RUNNING 2  /*!< Service Group is Running */

/**
 * \typedef stk_service_in_group_state
 * A 16 bit unsigned value which represents the state of a service *within* a group.
 * Applications may define their own states, but several are mandatory. 
 * \see STK_SERVICE_IN_GROUP_EXPECTED, STK_SERVICE_IN_GROUP_JOINED etc and stk_set_service_state_in_group()
 */
typedef stk_uint16 stk_service_in_group_state;

#define STK_SERVICE_IN_GROUP_EXPECTED 1  /*!< Service is expected in the group, but not joined */
#define STK_SERVICE_IN_GROUP_JOINED 2    /*!< Service has joined the group */
#define STK_SERVICE_IN_GROUP_ERROR 0xff  /*!< Service in the group is in an error state */

/** The callback signature to be used for functions being passed to stk_iterate_service_group() */
typedef stk_ret (*stk_service_in_group_cb)(stk_service_group_t *svc_group, stk_service_t *svc,void *clientd);
/**
 * The signature to be used for callback functions being registered to hear of services being added to a group.
 * \see stk_create_service_group()
 */
typedef void (*stk_service_added_cb)(stk_service_group_t *svc_group, stk_service_t *svc,stk_service_in_group_state state);
/**
 * The signature to be used for callback functions being registered to hear of services being removed from a group.
 * \see stk_create_service_group()
 */
typedef void (*stk_service_removed_cb)(stk_service_group_t *svc_group, stk_service_t *svc,stk_service_in_group_state state);
/**
 * The signature to be used for callback functions being registered to hear of smartbeats from services.
 * \see stk_create_service_group()
 */
typedef void (*stk_service_smartbeat_cb)(stk_service_group_t *svc_group, stk_service_t *svc, stk_smartbeat_t *smartbeat);

#endif

