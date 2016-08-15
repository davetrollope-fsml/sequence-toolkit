#include <stdio.h>
#include "stk_env_api.h"
#include "stk_test.h"

int main()
{
	TEST_ASSERT(sizeof(stk_uint64) == 8,"stk_uint64 is 8 bytes");
	TEST_ASSERT(sizeof(stk_uint32) == 4,"stk_uint32 is 4 bytes");
	TEST_ASSERT(sizeof(stk_uint16) == 2,"stk_uint16 is 2 bytes");
	TEST_ASSERT(sizeof(stk_uint8) == 1,"stk_uint8 is 1 bytes");

	TEST_ASSERT(sizeof(stk_service_id) == 8,"stk_service_id is 8 bytes");
	TEST_ASSERT(sizeof(stk_service_type) == 2,"stk_service_type is 2 bytes");
	TEST_ASSERT(sizeof(stk_service_state) == 1,"stk_service_state is 1 bytes");
	TEST_ASSERT(sizeof(stk_checkpoint_t) == 8,"stk_checkpoint_t is 8 bytes");

	TEST_ASSERT(sizeof(stk_smartbeat_t) == 24,"stk_smartbeat_t is 24 bytes");

	return 0;
}
