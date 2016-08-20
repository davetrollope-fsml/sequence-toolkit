#ifndef STK_EXAMPLES_H
#define STK_EXAMPLES_H
/* This header contains some utilities that are required to provide
 * clean example code so you can focus on the STK API and not 
 * distracting issues such as errpr checking. Feel free to use
 * them but they arent required to use STK.
 */

#include "assert.h"
#include "stdio.h"
#include "stdlib.h"

#define STK_ASSERT2(_fatal,_expr,...) do { \
	if(!(_expr)) { \
		fprintf(stderr,__VA_ARGS__); \
		fprintf(stderr,"\n"); \
		if(_fatal) assert(_expr); \
	} \
} while(0)

#define STK_ASSERT(_expr,...) STK_ASSERT2(1,_expr,__VA_ARGS__) 

#define STK_CHECK(_expr,...) STK_ASSERT2(0,_expr,__VA_ARGS__) 

#define STK_CHECK_RET(_expr,_rc,...) do { STK_ASSERT2(0,_expr,__VA_ARGS__); \
	if(!(_expr)) return (_rc); \
} while(0)

#define STK_LOG(_level,...) do { \
	fprintf(stderr,__VA_ARGS__); \
	fprintf(stderr,"\n"); \
	} while(0)

#define STK_MONITOR_OPTS \
	char *monitor_ip; \
	char *monitor_port; \
	char *monitor_name; \
	char monitor_protocol[7];

#define process_monitoring_string(_opts,_str) do { \
	stk_protocol_def_t _pdef; stk_data_flow_parse_protocol_str(&_pdef,_str); \
	if(_pdef.protocol[0] != '\0') strcpy((_opts)->monitor_protocol,_pdef.protocol); \
	if(_pdef.ip[0] != '\0') (_opts)->monitor_ip = strdup(_pdef.ip); \
	if(_pdef.port[0] != '\0') (_opts)->monitor_port = strdup(_pdef.port); \
	if(_pdef.name[0] != '\0') (_opts)->monitor_name = strdup(_pdef.name); \
	} while(0)

#define STK_NAME_SERVER_OPTS \
	char *name_server_ip; \
	char *name_server_port; \
	char name_server_protocol[5];

#define process_name_server_string(_opts,_str) do { \
	stk_protocol_def_t _pdef; stk_data_flow_parse_protocol_str(&_pdef,_str); \
	if(_pdef.protocol[0] != '\0') strcpy((_opts)->name_server_protocol,_pdef.protocol); \
	if(_pdef.ip[0] != '\0') (_opts)->name_server_ip = strdup(_pdef.ip); \
	if(_pdef.port[0] != '\0') (_opts)->name_server_port = strdup(_pdef.port); \
	} while(0)

#define STK_BIND_OPTS \
	char *bind_ip; \
	char *bind_port;

#define process_bind_string(_opts,_str) do { \
	stk_protocol_def_t _pdef; stk_data_flow_parse_protocol_str(&_pdef,_str); \
	if(_pdef.ip[0] != '\0') (_opts)->bind_ip = strdup(_pdef.ip); \
	if(_pdef.port[0] != '\0') (_opts)->bind_port = strdup(_pdef.port); \
	} while(0)

#define name_lookup_and_dispatch(_stkbase,_name,_cb,_expired,_cbs_rcvd,_subscribe) \
	do { \
		stk_ret _ret; _cbs_rcvd = 0; _expired = 0; \
		/* Request the monitoring IP/Port from the name server */ \
		if(_subscribe) \
			_ret = stk_subscribe_to_name_info(stk_env_get_name_service(_stkbase), _name, _cb, _stkbase, NULL); \
		else \
			_ret = stk_request_name_info(stk_env_get_name_service(_stkbase), _name, 1000, _cb, _stkbase, NULL); \
		STK_ASSERT(_ret==STK_SUCCESS,"Failed to request name '%s' %d",_name,_ret); \
		/* Dispatch to process name request response */ \
		while(_cbs_rcvd == 0 && _expired == 0) client_dispatcher_timed(default_dispatcher(),_stkbase,NULL,100); \
		if(_expired == 1 && _cbs_rcvd == 1) { \
			printf("Could not resolve %s\n",_name); \
			exit(5); \
		} \
		/* Wait for request to expire, for clarity following subsequent code and output */ \
		printf("Waiting to complete request\n"); \
		while((!_subscribe) && _expired == 0) client_dispatcher_timed(default_dispatcher(),_stkbase,NULL,500); \
	} while(0)

#endif
