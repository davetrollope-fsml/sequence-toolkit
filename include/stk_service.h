/** @file stk_service.h
 * This header provides the basic definitions to interact with Service APIs
 */
#ifndef STK_SERVICE_H
#define STK_SERVICE_H

#include "stk_common.h"

/**
 * \typedef stk_service_id
 * A 64 bit unsigned int which represents a service ID.
 * Applications may use any ID they decide appropriate for their service.
 * \see stk_create_service()
 */
typedef stk_uint64 stk_service_id;

/**
 * \typedef stk_service_type
 * A 16 bit unsigned int which represents a service type.
 * Applications may use any type they decide appropriate for their service,
 * though several are provided as standard
 * \see STK_SERVICE_TYPE_DATA, STK_SERVICE_TYPE_MGMT etc
 */
typedef stk_uint16 stk_service_type;

/* Predefined Service Types */
#define STK_SERVICE_TYPE_IO 1     /*!< I/O Service Type */
#define STK_SERVICE_TYPE_DATA 2   /*!< Data Service Type */
#define STK_SERVICE_TYPE_MGMT 3   /*!< Management Service Type */
#define STK_SERVICE_TYPE_NET 4    /*!< Network Service Type */

/* Defines for automated service notifications */
#define STK_SERVICE_NOTIF_CREATE 0xe00  /*!< Service Create Notification */
#define STK_SERVICE_NOTIF_DESTROY 0xe01 /*!< Service Destroy Notification */
#define STK_SERVICE_NOTIF_STATE 0xe02   /*!< Service State Notification */

/**
 * \typedef stk_service_state
 * An 8 bit unsigned value which represents the state of a service.
 * Applications may define their own states, but several are mandatory. 
 * \see STK_SERVICE_STATE_STARTING, STK_SERVICE_STATE_RUNNING etc and stk_set_service_state()
 */
typedef stk_uint8 stk_service_state;

#define STK_SERVICE_STATE_INVALID 0   /*!< Value 0 is deliberately not used - thus invalid */
#define STK_SERVICE_STATE_STARTING 1  /*!< Service is starting - initial state of service at creation */
#define STK_SERVICE_STATE_RUNNING 2   /*!< Service is Running */
#define STK_SERVICE_STATE_STOPPING 3  /*!< Service is Stopping */
#define STK_SERVICE_STATE_STOPPED 4   /*!< Service is Stopped */
#define STK_SERVICE_STATE_TIMED_OUT 5 /*!< Service timed out */

#define STK_SERVICE_STATE_NAME_MAX 80 /*!< Max length of a state name registered in a service */

#define STK_SERVICE_GROUP_NAME 0x8110 /*!< Service Group Name in Sequence */

/**
 * \typedef stk_service_t
 * A handle used to access a Sequence Service
 * \see stk_create_service()
 */
typedef struct stk_service_stct stk_service_t;

/**
 * The signature to be used for callback functions being registered to hear of service state changes
 * \see stk_create_service()
 */
typedef void (*stk_service_state_change_cb)(stk_service_t *svc,stk_service_state old_state,stk_service_state new_state);
#endif


