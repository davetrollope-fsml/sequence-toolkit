/** @file stk_sg_automation_api.h
 * The Service Group Automation module provides APIs to manage services in groups
 * remotely.
 *
 * This API allows automated creation and management of a service group.
 * Sequences containing user data allow creation and destruction of
 * services within a group and also support state changes.
 *
 * The following definitions specify the user-data types for service
 * group management.
 *
 * stk_sg_invoke_sequence() executes the required behaviour
 *
 * The sequence provided may come from the data sharing layer or a file
 */
#ifndef STK_SG_AUTOMATION_API_H
#define STK_SG_AUTOMATION_API_H
#include "stk_sequence.h"
#include "stk_data_flow.h"
#include "stk_service_group.h"


#define STK_SGA_CREATE_SVC 1   /*!< Create Service Instance Operation */
#define STK_SGA_DESTROY_SVC 2  /*!< Destroy Service Instance Operation */

/**
 * Invoke a Sequence on a Service Group.
 *
 * If the Sequence contains an operation (E.G. STK_SGA_CREATE_SVC ), it
 * will be executed. Otherwise the sequence will be ignored.
 * \returns Whether the sequence was successfully examined
 */
stk_ret stk_sga_invoke(stk_service_group_t *svcgrp, stk_sequence_t *seq);

/**
 * Add a service operation to a sequence.
 * \returns Whether the operation was successfully added to the sequence
 */
stk_ret stk_sga_add_service_op_to_sequence(stk_sequence_t *seq,stk_service_t *svc,stk_uint32 optype);

/**
 * Add a service state operation to a sequence.
 * Receiving a service state requires a name in the sequence - added implicitly in
 * a create or with stk_sga_add_service_name_to_sequence()
 * \returns Whether the operation was successfully added to the sequence
 */
stk_ret stk_sga_add_service_state_to_sequence(stk_sequence_t *seq,stk_service_t *svc,stk_service_state state);

/**
 * Add a service state name to a sequence.
 * \returns Whether the state name was successfully added to the sequence
 */
stk_ret stk_sga_add_service_state_name_to_sequence(stk_sequence_t *seq,stk_service_t *svc,stk_service_state state);


/**
 * Add a service name to a sequence.
 * \returns Whether the service name was successfully added to the sequence
 */
stk_ret stk_sga_add_service_name_to_sequence(stk_sequence_t *seq,stk_service_t *svc);

/**
 * Add the IP address for a data flow to a sequence as the reporting IP (and port)
 * using a reference element in the sequence. The caller must free the returned space.
 * \returns A malloc'd pointer to the address.
 */
struct sockaddr_in * stk_sga_add_service_reporting_ip_ref(stk_sequence_t *seq,stk_data_flow_t *df);

/**
 * Register the service group name with the name server, using the data flow as the identifying IP
 * \returns If the request was successfully sent to the name server (not that it was registered)
 */
stk_ret stk_sga_register_group_name(stk_service_group_t *svcgrp,stk_data_flow_t *df);
#endif
