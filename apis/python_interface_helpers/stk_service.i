%include "../../include/stk_smartbeat.h"
%include "../../include/stk_options.h"
%cstring_output_maxsize(char *state_str, size_t sz)
void stk_get_service_state_str_sz(stk_service_t *svc,stk_service_state state,char *state_str,size_t sz);
