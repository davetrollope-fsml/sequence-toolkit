/** @file stk_tcp_client_api.h
 * The TCP client module provides TCP connectivity to applications.
 * This module implements the data flow interface specified by the 
 * Sequence Toolkit. Applications should use the create API
 * defined here, but use the standard destroy/send/receive
 * APIs in stk_data_flow_api.h
 * Applicatipns may receive or send sequences on data flows.
 */
#ifndef STK_TCP_CLIENT_API_H
#define STK_TCP_CLIENT_API_H
#include "stk_env.h"
#include "stk_data_flow.h"

/**
 * Create a data flow for a TCP Client (connection).
 * \see stk_options.txt for accepted options
 * \see stk_data_flow.h
 * \returns A handle to a Sequence Data Flow
 */
stk_data_flow_t *stk_tcp_client_create_data_flow(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options);
#define stk_tcp_create_subscriber stk_tcp_client_create_data_flow /*!< Alias for Subscribers */

/**
 * Get the File Descriptor for a data flow. Applications may use this to
 * register with an event processing loop which calls select()/poll() etc.
 * \returns The File Descriptor.
 */
int stk_tcp_client_fd(stk_data_flow_t *svr_df);

/**
 * Get the IP acting as the ID for this data flow.
 * \returns Whether the IP address was filled out
 */
stk_ret stk_tcp_client_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen);

/**
 * Get the server IP for this data flow.
 * \returns Whether the IP address was filled out
 */
stk_ret stk_tcp_client_data_flow_serverip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen);

/** 
 * Force closing of resources related to a data flow without closing the data flow (E.G. closing sockets)
 * \returns Whether unhooking the related resources succeeded
 */
stk_ret stk_tcp_client_unhook_data_flow(stk_data_flow_t *df);
#endif
