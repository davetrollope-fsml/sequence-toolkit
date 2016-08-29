extern "C" {
	void stk_destroy_service_with_state(stk_service_t *svc,short state) {
		stk_service_state last_state = (stk_service_state) state;
		stk_destroy_service(svc,&last_state);
	}

	void stk_get_service_state_str_sz(stk_service_t *svc,stk_service_state state,char *state_str,size_t sz) {
		return stk_get_service_state_str(svc,state,state_str,sz);
	}
}
