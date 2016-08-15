/** @file stk_udp.h
 * A UDP based data flow module to enable services, service groups and
 * other Sequence Toolkit components to pass data over UDP.
 */
#ifndef STK_UDP_H
#define STK_UDP_H
#include "stk_common.h"
#include <sys/socket.h>
#include <netinet/in.h>

#define STK_UDP_SEND_FLAG_REUSE_GENID 0x1         /*!< Flag to prevent a send call from bumping a sequence generation to support multiple sends */
#define STK_UDP_SEND_FLAG_NONBLOCK 0x10           /*!< Flag to prevent a send call from blocking */

/**
 * \typedef stk_udp_wire_read_buf_t
 * Structure used to receive raw data when using stk_rawudp_listener_recv()
 */
typedef struct stk_udp_wire_read_buf_stct {
	stk_uint64 read;
	char buf[64*1024];
	struct sockaddr_in from_address;
	socklen_t from_address_len;
} stk_udp_wire_read_buf_t;
#endif
