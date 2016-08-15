/** @file stk_name_service_api.h
 * The Name service is a generic service to register and query names.
 * Each environment connects to a name server by default, but
 * applications may create their own.
 * Name service creation will initiate a connection to the name server.
 */
#ifndef STK_NAME_SERVICE_API
#include "stk_name_service.h"
#include "stk_env.h"
#include "stk_data_flow.h"
#include "stk_common.h"
#include "stk_sequence.h"

/**
 * API to retrieve information about a name from the name service
 * This API may cause repeated callbacks
 *
 * \param named The STK Name Service to be used
 * \param name The name to be queried for
 * \param expiration_sz The number of milliseconds this request should live for (Must be >0)
 * \param cb The callback to be executed when info is received
 * \param app_info Application data pointer for this request (passed to cb)
 * \param options Options used to configure the server and the information requested
 * \returns STK_SUCCESS if a name server is connected and the request was sent
 * \see stk_env_get_name_service()
 */
stk_ret stk_request_name_info(stk_name_service_t *named, char *name, int expiration_ms, stk_name_info_cb_t cb, void *app_info, stk_options_t *options);


/**
 * API to subscribe to name updates from the name service. Subscriptions expire after 24 hours.
 * Subscriptions will be reestablished as soon as possible after a name server restart,
 * but this may be up to one minute depending on the network transport used.
 * This API is designed to cause repeated callbacks.
 *
 * \param named The STK Name Service to be used
 * \param name The name to be queried for
 * \param cb The callback to be executed when info is received
 * \param app_info Application data pointer for this request (passed to cb)
 * \param options Options used to configure the server and the information requested
 * \returns STK_SUCCESS if a name server is connected and the request was sent
 * \see stk_env_get_name_service()
 */
stk_ret stk_subscribe_to_name_info(stk_name_service_t *named, char *name, stk_name_info_cb_t cb, void *app_info, stk_options_t *options);

/**
 * API to retreive information about a name from the name service
 * This API will cause at most 1 callback
 * \see stk_request_name_info()
 * \see stk_env_get_name_service()
 */
stk_ret stk_request_name_info_once(stk_name_service_t *named, char *name, int expiration_ms, stk_name_info_cb_t cb, void *app_info, stk_options_t *options);

/**
 * API to register a name with the name service
 * \param env The STK environment to be used
 * \param name The name to be registered
 * \param linger The number of seconds this name should live after being disconnected
 * \param expiration_sz The number of milliseconds this request should live for (Must be >0)
 * \param cb The callback to be executed when the registration response is received
 * \param options Options used to configure this name at the name server
 */
stk_ret stk_register_name(stk_name_service_t *named,char *name, int linger, int expiration_ms, stk_name_info_cb_t cb, void *app_info, stk_options_t *options);

/**
 * API to create a name service
 * \param env The STK environment to be used
 * \param options Options used to configure this name service connection
 */
stk_name_service_t *stk_create_name_service(stk_env_t *env, stk_options_t *options);

/**
 * API to destroy a name service
 */
stk_ret stk_destroy_name_service(stk_name_service_t *);

/**
 * API to invoke name services on a received sequence
 */
stk_ret stk_name_service_invoke(stk_sequence_t *seq);

/**
 * Get array of flows that need to be sent to for heartbeats.
 * Array ends with NULL
 * Caller must free returned data with free()
 */
stk_data_flow_t **stk_ns_get_smartbeat_flows(stk_name_service_t *svc);
#endif
