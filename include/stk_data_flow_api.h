/** @file stk_data_flow_api.h
 * The data flow API is an abstraction which other modules implement.
 * However, the APIs in this file are used by implementations to
 * provide a common framework and presentation to other STK modules.
 *
 * If an STK MODULE doesn't support a capability and the data flow API is called,
 * the API will return STK_NOT_SUPPORTED (or NULL for pointer types)
 */
#ifndef STK_DATA_FLOW_API_H
#define STK_DATA_FLOW_API_H
#include "stk_env.h"
#include "stk_data_flow.h"

/**
 * Allocate a STK Data Flow handle
 * The flow_type is an identifier to differentiate the types of data flow (typically a module type).
 * STK provides types, but applications may use their own as they develop data flow modules.
 * The name and id are application provided identifiers for the data flow.
 * The extendedsz defines how much module specific size should be allocated for this data flow.
 * fptrs is an array of function pointers which implement the data flow.
 * \returns A data flow handle
 * \see stk_data_flow_module_data()
 */
stk_data_flow_t *stk_alloc_data_flow(stk_env_t *env,stk_uint16 flow_type,char *name,stk_data_flow_id id,int extendedsz,stk_data_flow_module_t *fptrs,stk_options_t *options);
/**
 * Add a hold to the data flow.
 *
 * This bumps a refcnt. Use stk_free_data_flow() to release the hold
 * \see stk_free_data_flow();
 */
void stk_hold_data_flow(stk_data_flow_t *df);
/**
 * Free a STK Data Flow
 * \returns Whether the data flow was freed successfully
 */
stk_ret stk_free_data_flow(stk_data_flow_t *df);
/**
 * Get the custom module address for extended space allocated as part of stk_alloc_data_flow()
 * \see stk_alloc_data_flow()
 * \returns The custom module data space pointer
 */
void *stk_data_flow_module_data(stk_data_flow_t *df);
/**
 * Get the data flow error code.
 * \returns the errno related to this data flow
 */
int stk_data_flow_errno(stk_data_flow_t *df);
/**
 * Set the data flow error code.
 */
void stk_set_data_flow_errno(stk_data_flow_t *df,int newerrno);
/**
 * Get the STK Environment for a data flow.
 * \returns The STK Environment pointer
 */
stk_env_t *stk_env_from_data_flow(stk_data_flow_t *df);
/**
 * Get the data flow ID from a data flow
 * \returns The data flow ID
 */
stk_data_flow_id stk_get_data_flow_id(stk_data_flow_t *df);
/**
 * Get the data flow type from a data flow
 * \returns The data flow type
 */
stk_uint16 stk_get_data_flow_type(stk_data_flow_t *df);
/**
 * Get the data flow name
 * \returns A reference pointer to the data flow name
 */
char *stk_data_flow_name(stk_data_flow_t *df);

/**
 * Abstract API to destroy a data flow
 * \returns Whether the destruction succeeded
 */
stk_ret stk_destroy_data_flow(stk_data_flow_t *df);

/**
 * Abstract API to send data on any data flow module based on the function pointers passed to
 * data flow creation.
 * \returns Whether the send succeeded
 */
stk_ret stk_data_flow_send(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);

/**
 * Abstract API to receive data on any data flow module based on the function pointers passed to
 * data flow creation.
 * \returns A received sequence or NULL
 */
stk_sequence_t *stk_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);

/**
 * Abstract API to get the identifying IP on any data flow module based on the function pointers passed to
 * data flow creation.
 * \returns Whether the ID was filled out
 */
stk_ret stk_data_flow_id_ip(stk_data_flow_t *flow,struct sockaddr *data_flow_id,socklen_t addrlen);

/**
 * Get the identifying IP on any data flow in network order
 * \returns Whether the ID was filled out
 * \see stk_data_flow_id_ip
 */
stk_ret stk_data_flow_id_ip_nw(stk_data_flow_t *df,struct sockaddr_in *data_flow_id,socklen_t addrlen);

/**
 * Abstract API to get the protocol string for any data flow (assuming the module implements this API)
 * \returns Protocol string
 */
char *stk_data_flow_protocol(stk_data_flow_t *flow);

/**
 * Abstract API for data flows to provide an indication whether data is buffered
 * \returns STK_SUCCESS if data is buffered
 */
stk_ret stk_data_flow_buffered(stk_data_flow_t *df);

/**
 * API to get the client IP from a sequence for the data flow it was received on.
 * Note: This API converts to host order
 * \returns STK_SUCCESS if the client IP was filled out
 */
stk_ret stk_data_flow_client_ip(stk_sequence_t *seq,struct sockaddr_in *client_ip,socklen_t *addrlen);

/**
 * API to add the client IP to a sequence
 * \returns STK_SUCCESS if the client IP was added
 */
stk_ret stk_data_flow_add_client_ip(stk_sequence_t *seq,struct sockaddr_in *client_ip,socklen_t addrlen);

/**
 * API to get the client protcol from a sequence for the data flow it was received on.
 * \returns STK_SUCCESS if the client IP was filled out
 */
stk_ret stk_data_flow_client_protocol(stk_sequence_t *seq,char *protocol_ptr, stk_uint64 *len);

/**
 * API to add the client protcol to a sequence
 * \returns STK_SUCCESS if the client IP was added
 */
stk_ret stk_data_flow_add_client_protocol(stk_sequence_t *seq,char *protocol);

/**
 * Utility to find a data flow option and do all the necessary work for data flow creation and auto create it if needed.
 * Pass in a data flow option name like "monitoring_data_flow" and if that option exists in the set, it is returned.
 * Otherwise, "monitoring_data_flow_options" is search for as parameters to create a data flow using the create_data_flow function
 * \returns The data flow that is configured or created
 */
stk_data_flow_t *stk_data_flow_process_extended_options(stk_env_t *env, stk_options_t *options, char *option_name, stk_create_data_flow_t create_data_flow);

/**
 * Utility to help parse protocol strings.
 * [protocol:]<ip|name>[:port]
 * where protocol may be <tcp|udp|rawudp|multicast>
 */
void stk_data_flow_parse_protocol_str(stk_protocol_def_t *def,char *str);
#endif
