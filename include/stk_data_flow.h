/** @file stk_data_flow.h
 * This file defines the abstract interface for data flow modules.
 * This interface is used by all modules to intercommunicate.
 *
 * This header defines the common set of APIs that is needed to share data.
 * This includes setting up a data flow and exchanging data.
 * These APIs form the foundation for other STK modules to share data
 * and thus provide a means for other middleware to share data
 */
#ifndef STK_DATA_FLOW_H
#define STK_DATA_FLOW_H
#include "stk_env.h"
#include "stk_common.h"
#include "stk_sequence.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

/**
 * \typedef stk_data_flow_t
 * This is the Sequence Data Flow object
 */
typedef struct stk_data_flow_stct stk_data_flow_t;
/**
 * \typedef stk_data_flow_id
 * A Data Flow ID is a 64 bit unsigned number.
 * Applications may choose their own IDs
 */
typedef stk_uint64 stk_data_flow_id;

/** The signature to be used for data flow modules implementing the Create Data Flow API */
typedef stk_data_flow_t *(*stk_create_data_flow_t)(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options);
/** The signature to be used for data flow modules implementing the Destroy Data Flow API */
typedef stk_ret (*stk_destroy_data_flow_t)(stk_data_flow_t *flow);
/** The signature to be used for data flow modules implementing the Data Flow Send API */
typedef stk_ret (*stk_data_flow_send_t)(stk_data_flow_t *flow,stk_sequence_t *data_sequence,stk_uint64 flags);
/** The signature to be used for data flow modules implementing the Data Flow Receive API */
typedef stk_sequence_t *(*stk_data_flow_rcv_t)(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);
/** The signature to be used for data flow modules implementing the Data Flow IP ID API */
typedef stk_ret (*stk_data_flow_identifying_ip_t)(stk_data_flow_t *flow,struct sockaddr *data_flow_id,socklen_t addrlen);
/** The signature to be used for data flow modules implementing the Data Buffered API */
typedef stk_ret (*stk_data_flow_buffered_t)(stk_data_flow_t *flow);
/** The signature to be used for data flow modules implementing the protocol API */
typedef char * (*stk_data_flow_protocol_t)(stk_data_flow_t *flow);

/**
 * The interface for data flow modules.
 * Implementors should create an instance of this module structure with their APIs and pass this to
 * other STK modules to share data.
 */
typedef struct stk_data_flow_module_stct {
	stk_create_data_flow_t create_data_flow;           /*!< Function pointer to the data flow module create implementation */
	stk_destroy_data_flow_t destroy_data_flow;         /*!< Function pointer to the data flow module destroy implementation */
	stk_data_flow_send_t data_flow_send;               /*!< Function pointer to the data flow module send implementation */
	stk_data_flow_rcv_t data_flow_rcv;                 /*!< Function pointer to the data flow module receive implementation */
	stk_data_flow_identifying_ip_t data_flow_id_ip;    /*!< Function pointer to the data flow module IP ID implementation */
	stk_data_flow_buffered_t data_flow_buffered;       /*!< Function pointer to the data flow module buffered implementation */
	stk_data_flow_protocol_t data_flow_protocol;       /*!< Function pointer to the data flow module protocol implementation */
} stk_data_flow_module_t;


/**
 * Callback executed when a data flow is removed
 */
typedef void (*stk_data_flow_destroyed_cb)(stk_data_flow_t *flow,stk_data_flow_id id);
/**
 * Callback executed when a file descriptor is added
 */
typedef void (*stk_data_flow_fd_created_cb)(stk_data_flow_t *flow,stk_data_flow_id id,int fd);
/**
 * Callback executed when a file descriptor is removed
 */
typedef void (*stk_data_flow_fd_destroyed_cb)(stk_data_flow_t *flow,stk_data_flow_id id,int fd);

/* Passed to stk_alloc_data_flow() and returned by stk_get_data_flow_type() */
#define STK_TCP_SERVER_FLOW 1      /*!< The Data Flow Type for server TCP data flows */
#define STK_TCP_CLIENT_FLOW 2      /*!< The Data Flow Type for client TCP data flows */
#define STK_RAWUDP_LISTENER_FLOW 3 /*!< The Data Flow Type for raw listening UDP data flows */
#define STK_RAWUDP_CLIENT_FLOW 4   /*!< The Data Flow Type for raw client UDP data flows */
#define STK_UDP_LISTENER_FLOW 5    /*!< The Data Flow Type for listening UDP data flows */
#define STK_UDP_CLIENT_FLOW 6      /*!< The Data Flow Type for client UDP data flows */
#define STK_TCP_ACCEPTED_FLOW 7    /*!< The Data Flow Type for server accepted TCP data flows */

typedef struct stk_protocol_def_stct
{
	char protocol[16];
	char ip[64];
	char port[6];
	char name[64]; /* name of IP/Port info - TBD */
} stk_protocol_def_t;
#endif

