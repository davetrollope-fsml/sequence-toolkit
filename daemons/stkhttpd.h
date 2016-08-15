#ifndef STK_HTTPD_H
#define STK_HTTPD_H
#include "stk_sga_internal.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include "PLists.h"

typedef struct stk_collect_service_data_t_stct {
	stk_sga_service_inst_t svcinst;
	char *svc_name;
	stk_service_state state_update;
	int displaced;
	int inactivity;
	stk_smartbeat_t rcv_time;
	char *svc_grp_name;
	struct sockaddr_in ipaddr;
	char client_protocol[16];
	char *state_name;
} stk_collect_service_data_t;

stk_bool stk_timeout_service(stk_collect_service_data_t *svcdata,Node *n);

#endif
