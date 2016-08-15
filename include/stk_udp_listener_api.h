/** @file stk_udp_listener_api.h
 * The UDP listener module provides UDP listening sockets to applications.
 * This module implements the data flow interface specified by the 
 * Sequence Toolkit. Applications should use the create API
 * defined here, but use the standard destroy/send/receive
 * APIs in stk_data_flow_api.h
 * Applications may only receive on data flows.
 */
#ifndef STK_UDP_LISTENER_API_H
#define STK_UDP_LISTENER_API_H
#include "stk_data_flow.h"

/**
 * Create a UDP listening data flow
 * \see stk_options.txt for accepted options
 * \see stk_data_flow.h
 * \returns A handle to a Sequence Data Flow
 */
stk_data_flow_t *stk_udp_listener_create_data_flow(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options);
#define stk_udp_create_subscriber stk_udp_listener_create_data_flow /*!< Alias for Subscribers */
/**
 * Get the File Descriptor for a data flow. Applications may use this to
 * register with an event processing loop which calls select()/poll() etc.
 * \returns The File Descriptor.
 */
int stk_udp_listener_fd(stk_data_flow_t *svr_df);
/**
 * Receive data from a UDP data flow
 * \returns The sequence containing received data (if any)
 */
stk_sequence_t *stk_udp_listener_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);
/**
 * Get the IP acting as the ID for this data flow.
 * \returns Whether the IP address was filled out
 */
stk_ret stk_udp_listener_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen);
/**
 * Get the client IP for this data flow.
 * \returns Whether the IP address was filled out
 */
stk_ret stk_udp_listener_data_flow_clientip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen);

#endif
