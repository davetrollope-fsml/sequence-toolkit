/** @file stk_smartbeat.h
 * This file provides definitions and typdefs etc required for smartbeats
 */
#ifndef STK_SMARTBEAT_H
#define STK_SMARTBEAT_H

#include "stk_common.h"
#include "stk_service.h"
#include <sys/time.h>

/**
 * \typedef stk_checkpoint_t
 * The Smartbeat checkpoint is atomically (lock-free) incremented
 * so applications may use multiple threads to indicate liveness
 *
 * It is a 64 bit unsigned integer
 */
typedef stk_uint64 stk_checkpoint_t;

/**
 * The stk_smartbeat_t structure provides a time and checkpoint.
 * \see stk_service_last_smartbeat()
 */
typedef struct stk_smartbeat_stct {
	stk_uint64 sec;              /*!< Time of heartbeat (secs) */
	stk_uint64 usec;             /*!< Time of heartbeat (usecs) */
	stk_checkpoint_t checkpoint; /*!< Last checkpoint from app */
} stk_smartbeat_t;

/**
 * \typedef stk_smartbeat_ctrl_t
 * Control structure for smartbeat handling
 */
typedef struct stk_smartbeat_ctrl_stct stk_smartbeat_ctrl_t;

/** Sequence ID for smartbeats */
#define STK_SMARTBEAT_SEQ 0x3000000

/** ID of structure for data coming off the wire */
#define STK_STCT_SVC_SMARTBEAT_WIRE 0x50000001

/**
 * Structure of smartbeat on the wire
 */
typedef struct stk_smartbeat_svc_wire_stct {
	stk_service_id service;    /*!< Service ID for which this heartbeat applies */
	stk_smartbeat_t smartbeat; /*!< Heartbeat and checkpoint for this service */
} stk_smartbeat_svc_wire_t;

#endif
