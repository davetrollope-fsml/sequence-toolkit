/** @file stk_tcp_server_api.h
 * The TCP server module provides TCP listening sockets to applications.
 * This module implements the data flow interface specified by the 
 * Sequence Toolkit. Applications should use the create API
 * defined here, but use the standard destroy/send/receive
 * APIs in stk_data_flow_api.h
 * Applicatipns may receive or send sequences on data flows.
 */
#ifndef STK_TCP_SERVER_API_H
#define STK_TCP_SERVER_API_H
#include "stk_env.h"
#include "stk_data_flow.h"

/**
 * Create a data flow for a TCP Server (Listening Socket).
 * \see stk_options.txt for accepted options
 * \see stk_data_flow.h
 * \returns A handle to a Sequence Data Flow
 */
stk_data_flow_t *stk_tcp_server_create_data_flow(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options);
#define stk_tcp_create_publisher stk_tcp_server_create_data_flow /*!< Alias for Publishers */
/**
 * Accept a new data flow from the Server Data Flow.
 * \returns A new data flow handle for the accepted TCP connection.
 */
stk_data_flow_t *stk_tcp_server_accept(stk_data_flow_t *svr_df);
/**
 * Get the File Descriptor for a data flow. Applications may use this to
 * register with an event processing loop which calls select()/poll() etc.
 * \returns The File Descriptor.
 */
int stk_tcp_server_fd(stk_data_flow_t *svr_df);

/**
 * Send data on the TCP data flow
 * \returns Whether the data was successfully sent
 */
stk_ret stk_tcp_server_data_flow_send(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);

/**
 * Receive data from a TCP data flow
 * \returns The sequence containing received data (if any)
 */
stk_sequence_t *stk_tcp_server_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);

/**
 * Get the IP acting as the ID for this data flow.
 * \returns Whether the IP address was filled out
 */
stk_ret stk_tcp_server_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen);

/**
 * Get the client IP for this data flow.
 * \returns Whether the IP address was filled out
 */
stk_ret stk_tcp_server_data_flow_clientip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen);
#endif
