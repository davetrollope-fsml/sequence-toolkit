/** @file stk_rawudp_api.h
 * The raw UDP module provides UDP sockets to applications.
 * Note: This module does not carry sequence meta data - it is
 * primarily designed to interface to external systems.
 * Sequences received will only have 1 element and will be the UDP packet
 * This module implements the data flow interface specified by the 
 * Sequence Toolkit. Applications should use the create API
 * defined here, but use the standard destroy/send/receive
 * APIs in stk_data_flow_api.h
 * Applications may only receive on data flows.
 */
#ifndef STK_RAWUDP_API_H
#define STK_RAWUDP_API_H
#include "stk_udp.h"
#include "stk_data_flow.h"
#include <sys/socket.h>

/**
 * Create a raw UDP listening data flow
 * \see stk_options.txt for accepted options
 * \see stk_data_flow.h
 * \returns A handle to a Sequence Data Flow
 */
stk_data_flow_t *stk_rawudp_listener_create_data_flow(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options);
#define stk_rawudp_create_subscriber stk_rawudp_listener_create_data_flow /*!< Alias for Subscribers */
/**
 * Get the File Descriptor for a data flow. Applications may use this to
 * register with an event processing loop which calls select()/poll() etc.
 * \returns The File Descriptor.
 */
int stk_rawudp_listener_fd(stk_data_flow_t *svr_df);
/**
 * Receive data from a RAW UDP data flow
 * \returns The sequence containing received data (if any)
 */
stk_sequence_t *stk_rawudp_listener_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);
/**
 * Receive raw data from a RAW UDP data flow
 * \returns The bytes read (also stored in bufread)
 */
stk_uint64 stk_rawudp_listener_recv(stk_data_flow_t *df,stk_udp_wire_read_buf_t *bufread);
/**
 * Add the client IP to a sequence
 * \returns Whether the client IP was added
 */
stk_ret stk_rawudp_listener_add_client_ip(stk_data_flow_t *df,stk_sequence_t *seq,stk_udp_wire_read_buf_t *bufread);

/**
 * Get the IP acting as the ID for this data flow.
 * \returns Whether the IP address was filled out
 */
stk_ret stk_rawudp_listener_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen);
/**
 * Get the client IP for this data flow.
 * \returns Whether the IP address was filled out
 */
stk_ret stk_rawudp_listener_data_flow_clientip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen);
/**
 * Used to send raw data to the data flow with the destination address provided at creation
 * \returns STK_SUCCESS if the data was sent
 */
stk_ret stk_rawudp_listener_data_flow_sendbuf(stk_data_flow_t *df,char *buf,stk_uint64 buflen,stk_uint64 flags);
/**
 * Used to send raw data to the data flow to an alternate destination address
 * \returns STK_SUCCESS if the data was sent
 */
stk_ret stk_rawudp_listener_data_flow_send_dest(stk_data_flow_t *df,char *buf,stk_uint64 buflen,stk_uint64 flags,struct sockaddr_in *dest_addr,size_t sz);
/**
 * Create a raw UDP client (sender) data flow
 * \see stk_options.txt for accepted options
 * \see stk_data_flow.h
 * \returns A handle to a Sequence Data Flow
 */
stk_data_flow_t *stk_rawudp_client_create_data_flow(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options);
#define stk_rawudp_create_publisher stk_rawudp_client_create_data_flow /*!< Alias for Publishers */

/**
 * Get the File Descriptor for a data flow. Applications may use this to
 * register with an event processing loop which calls select()/poll() etc.
 * \returns The File Descriptor.
 */
int stk_rawudp_client_fd(stk_data_flow_t *df);
/**
 * Get the IP acting as the ID for this data flow.
 * \returns Whether the IP address was filled out
 */
stk_ret stk_rawudp_client_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen);
/**
 * Get the server IP for this data flow.
 * \returns Whether the IP address was filled out
 */
stk_ret stk_rawudp_client_data_flow_serverip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen);

#endif
