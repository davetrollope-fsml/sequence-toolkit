#ifndef STK_SGA_INTERNAL_H
#define STK_SGA_INTERNAL_H
#include "stk_common.h"
#include "stk_service.h"
#include "stk_smartbeat.h"

#define STK_STCT_SGA_SERVICE_INST 0x8100
#define STK_STCT_SGA_SERVICE_INST_NAME 0x8101
#define STK_STCT_SGA_SERVICE_STATE 0x8102
#define STK_STCT_SGA_SERVICE_IP_ID 0x8103
#define STK_STCT_SGA_SERVICE_STATE_NAME 0x8104

typedef struct stk_sga_service_inst_stct {
	stk_uint32 sz;
	stk_uint32 optype;
	stk_service_id id;
	stk_service_type type;
	stk_service_state state;
	stk_smartbeat_t smartbeat;
	stk_uint32 activity_tmo;
} stk_sga_service_inst_t;

#endif
