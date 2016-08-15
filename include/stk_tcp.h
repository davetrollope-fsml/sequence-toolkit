/** @file stk_tcp.h
 * A TCP based data flow module to enable services, service groups and
 * other Sequence Toolkit components to pass data over TCP.
 */
#ifndef STK_TCP_H
#define STK_TCP_H
#include "stk_common.h"

#define STK_TCP_SEND_FLAG_REUSE_GENID 0x1         /*!< Flag to prevent a send call from bumping a sequence generation to support multiple sends */
#define STK_TCP_SEND_FLAG_NONBLOCK 0x10           /*!< Flag to prevent a send call from blocking */

#endif
