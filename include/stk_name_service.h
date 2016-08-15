/** @file stk_name_service.h
 * This header provides the typedefs and definitions required to interface
 * to the Name Server APIs
 *
 * The Name service is a generic service to register and query names.
 * Applications should call stk_ret stk_configure_name_service()
 * which will initiate a connection to the name server ahead
 * of other API calls and provides a single API to update
 * the client's server configuration.
 */
#ifndef STK_NAME_SERVICE
#define STK_NAME_SERVICE
#include "stk_common.h"
#include "stk_sequence.h"
#include "netinet/in.h"

/**
 * \typedef stk_name_service_t
 * Structure used to hold control data about a name service
 */
typedef struct stk_name_service_stct stk_name_service_t;

#define STK_MAX_NAME_LEN 96          /*!< The max len for a name */
#define STK_MAX_GROUP_NAME_LEN 48    /*!< The max len for a group name */
#define STK_NAME_MAX_PROTOCOL_LEN 16 /*!< The max len for a protocol string */

/**
 * \typedef stk_name_ip_t
 * Structure used to hold the IP and protocol of a registered name
 */
typedef struct stk_name_ip_stct {
	struct sockaddr_in sockaddr;              /*!< An IP/Port */
	char protocol[STK_NAME_MAX_PROTOCOL_LEN]; /*!< String describing the protocol */
	char ipstr[INET_ADDRSTRLEN];              /*!< String version of the IP */
	char portstr[6];                          /*!< String version of the port */
} stk_name_ip_t;

/**
 * \typedef stk_name_ft_state_t
 * Enumeration describing tault tolerant state of a name
 */
typedef enum stk_name_ft_state_enum {
	STK_NAME_STANDBY, STK_NAME_ACTIVE
} stk_name_ft_state_t;

#define STK_NAME_MAX_IPS 5                  /*!< Maximum number of IPs assigned to a name */

/**
 * \typedef stk_name_info_t
 * Structure used to pass information about name from the name service
 */
typedef struct stk_name_info_stct {
	char name[STK_MAX_NAME_LEN];            /*!< Name the information is about */
	stk_name_ip_t ip[STK_NAME_MAX_IPS];     /*!< IPs registered with name */
	stk_name_ft_state_t ft_state;           /*!< fault tolerant state (active/standby) */
	stk_sequence_t *meta_data;              /*!< Meta data associated with this name */
} stk_name_info_t;

/**
 * \typedef stk_name_info_cb_type
 * Enum passed to name information callbacks indicating the type of callback
 */
typedef enum {
	STK_NS_REQUEST_RESPONSE,     /*!< Callback is a response from the name server */
	STK_NS_REQUEST_EXPIRED       /*!< Callback is the expiration of a request */
} stk_name_info_cb_type;

/**
 * Callback executed when name information arrives or a request expires
 */
typedef void (*stk_name_info_cb_t)(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info,stk_name_info_cb_type cb_type);

#define STK_NAME_REQUEST_SEQUENCE_NAME "name request"       /*!< Name of the sequence used to interact with the name service */
#define STK_NAME_REQUEST_SEQUENCE_ID 0x9a3e07e9             /*!< ID of the sequence used to interact with the name service */

#define STK_NS_SEQ_NAME 1                                   /*!< The Sequence item ID for names being registered/queried */
#define STK_NS_SEQ_CONNECT_IPV4 2                           /*!< The connection address (IPV4) for this name */
#define STK_NS_SEQ_GROUP_NAME 3                             /*!< The group name that applies to the registration/query */
#define STK_NS_SEQ_LINGER 4                                 /*!< The time a name should live after disconnection */
#define STK_NS_SEQ_REQUEST_ID 5                             /*!< Request ID sent to name service - used by clients for uniqueness */
#define STK_NS_SEQ_PROTOCOL 6                               /*!< Protocol for any connection info for this name */
#define STK_NS_SEQ_FT_STATE 7                               /*!< Fault Tolerant state (stk_ft_state_t) */
#define STK_NS_SEQ_ID 8                                     /*!< Name Server ID */

/* Well known Meta Data IDs and identifiers */
#define STK_MD_HTTPD_TCP_ID 0x1000       /*!< Meta Data ID for tcp connectivity to stkhttpd */
#define STK_MD_HTTPD_UDP_ID 0x1001       /*!< Meta Data ID for udp connectivity to stkhttpd */
#define STK_MD_HTTPD_MCAST_ID 0x1002     /*!< Meta Data ID for multicast connectivity to stkhttpd */
#define STK_HTTPD_DF_META_IDS "monitor"  /*!< Name used to get monitoring address info for stkhttpd */

/* Recommended Meta Data IDs */
#define STK_MD_IPV4 0x8000               /*!< Meta Data ID for IPV4 addresses, binary, network order */
#define STK_MD_IPV6 0x8001               /*!< Meta Data ID for IPV6 addresses, binary, network order */
#define STK_MD_Port 0x8002               /*!< Meta Data ID for Ports, binary */
#define STK_MD_DATA_FLOW_TYPE 0x8003     /*!< Meta Data ID for data flow type, binary */

#define STK_MD_SEQUENCE_ID 0x8100        /*!< Meta Data ID for sequence IDs */
#define STK_MD_DATA_FLOW_ID 0x8101       /*!< Meta Data ID for dataflow IDs */
#define STK_MD_SERVICE_ID 0x8102         /*!< Meta Data ID for service IDs */
#define STK_MD_SERVICE_GROUP_ID 0x8103   /*!< Meta Data ID for service group IDs */

#endif
