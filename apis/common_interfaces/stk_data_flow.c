extern "C" {
	stk_data_flow_t *stk_ulong_df_to_df_ptr(unsigned long ptr) { return (stk_data_flow_t *) ptr; }
	unsigned long stk_df_ptr_to_ulong_df(stk_data_flow_t *ptr) { return (unsigned long) ptr; }
	stk_uint16 stk_dftype_ulong(unsigned long ptr) { return stk_get_data_flow_type((stk_data_flow_t *) ptr); }
}
