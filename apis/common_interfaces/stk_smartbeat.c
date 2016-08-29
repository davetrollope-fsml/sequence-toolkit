extern "C" {
	#include "../../include/stk_smartbeat.h"

	stk_checkpoint_t stk_smartbeat_checkpoint(stk_smartbeat_t *smartbeat)
	{ return smartbeat->checkpoint; }
}
