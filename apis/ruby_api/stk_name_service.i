%feature("director") stk_name_service_cb_class;
%include "../../include/stk_name_service_api.h"
%include "../../include/stk_name_service.h"
%include "../../include/stk_sequence.h"
%include "stk_name_service_cb.h"
%cstring_output_allocate(char **name, 0);
void stk_name_from_name_info(stk_name_info_t *name_info, char **name);
%cstring_bounded_output(char *ip,16);
void stk_rip_from_name_info(stk_name_info_t *name_info,int idx,char *ip);
stk_ret stk_register_name_nocb(stk_name_service_t *named,char *name, int linger, int expiration_ms, void *app_info, stk_options_t *options);
