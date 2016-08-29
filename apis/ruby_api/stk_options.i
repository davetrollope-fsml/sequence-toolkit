%inline %{ stk_options_t *find_sub_option(stk_options_t *options, char *name) { return (stk_options_t *) stk_find_option(options,name,NULL); } %}
%include "../../include/stk_data_flow.h"
%include "../../include/stk_sequence.h"

