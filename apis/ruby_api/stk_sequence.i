%include "../include/stk_env.h"
%include "../include/stk_service.h"
%include "../include/stk_sequence_api.h"
%include "../include/stk_sequence.h"
%cstring_output_allocate_size(char **dptr, stk_uint64 *sz, 0)
void stk_sequence_node_data(Node *n,char **dptr,stk_uint64 *sz);
%apply (char *STRING, size_t LENGTH) { (char *data_ptr, int sz) };
stk_ret stk_copy_string_to_sequence(stk_sequence_t *seq,char *data_ptr,int sz,stk_uint64 user_type);
