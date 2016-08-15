/** @file stk_ports.h
 * This file provides default port numbers for various STK components
 */
#ifndef STK_PORTS
#define STK_PORTS

#define STK_HTTPD_RCV_DF_PORT 20001 /*!< Default Web Monitor data flow port to receive data on */
#define STK_NAMED_RCV_DF_PORT 20002 /*!< Default Name Service data flow port to receive data on */

#define STK_DEFAULT_TCP_SERVER_PORT 29090 /*!< Default data flow port to accept tcp data on */
#define STK_DEFAULT_RAWUDP_LISTENER_PORT 29190 /*!< Default data flow port to receive raw udp data on */

#endif
