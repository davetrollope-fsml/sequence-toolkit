/** @file stk_mixed_connection_api.h
 * The Mixed Connection module provides the ability to combine
 * listener and client data flows as a single data flow.
 * Currently it only supports UDP listeners and clients, but
 * in the future will support mixing TCP and UDP listeners/clients.
 *
 * This module implements the data flow interface specified by the 
 * Sequence Toolkit. Applications should use the create API
 * defined here, but use the standard destroy/send/receive
 * APIs in stk_data_flow_api.h
 * Applications may only send sequences on data flows.
 */
#ifndef STK_MIXED_CONNECTION_API_H
#define STK_MIXED_CONNECTION_API_H
#include "stk_data_flow.h"

/**
 * Create a  Mixed connection (sender) data flow
 * \see stk_options.txt for accepted options
 * \see stk_data_flow.h
 * \returns A handle to a Sequence Data Flow
 */
stk_data_flow_t *stk_mixed_connection_create_data_flow(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options);
/**
 * Receive data from a mixed connection data flow
 * \returns The sequence containing received data (if any)
 */
stk_sequence_t *stk_mixed_connection_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);
/**
 * Get the Listener File Descriptor for a data flow. Applications may use this to
 * register with an event processing loop which calls select()/poll() etc.
 * \returns The Listener File Descriptor.
 */
int stk_mixed_connection_listener_fd(stk_data_flow_t *df);
/**
 * Get the Client File Descriptor for a data flow. Applications may use this to
 * register with an event processing loop which calls select()/poll() etc.
 * \returns The Client File Descriptor.
 */
int stk_mixed_connection_client_fd(stk_data_flow_t *df);
/**
 * Get the IP acting as the ID for this data flow.
 * \returns Whether the IP address was filled out
 */
stk_ret stk_mixed_connection_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen);

#endif
