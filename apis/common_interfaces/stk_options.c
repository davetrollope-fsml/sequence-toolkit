stk_options_t *stk_void_to_options_t(void *options) { return (stk_options_t *) options; }
extern "C" {
#include "../../include/stk_env.h"
#include "../../include/stk_data_flow.h"
#include "../../include/stk_sequence.h"
	void wakeup_dispatcher(stk_env_t *env);
	void *stk_option_to_void(stk_options_t *options) { return options; }
	stk_options_t *stk_append_data_flow(stk_options_t *opts,char *name,stk_data_flow_t *df) {
		stk_options_t *e = stk_copy_extend_options(opts, 1);
		stk_append_option(e,strdup(name),(void*) df);
		return e;
	}
	stk_options_t *stk_append_sequence(stk_options_t *opts,char *name,stk_sequence_t *seq) {
		stk_options_t *e = stk_copy_extend_options(opts, 1);
		stk_append_option(e,strdup(name),(void*) seq);
		return e;
	}
	stk_options_t *stk_append_dispatcher_wakeup_cb(stk_options_t *opts) {
		stk_options_t *e = stk_copy_extend_options(opts, 1);
		stk_append_option(e,strdup("wakeup_cb"),(void*) wakeup_dispatcher);
		return e;
	}
	void stk_clear_cb(stk_options_t *curr_option,char *name) {
		stk_update_option(curr_option, name, NULL, NULL);
	}
	stk_options_t *stk_append_dispatcher(stk_options_t *opts,unsigned long dispatcher) {
		stk_options_t *e = stk_copy_extend_options(opts, 1);
		stk_append_option(e,strdup("dispatcher"),(void*) dispatcher);
		return e;
	}
	extern stk_options_t *stk_append_dispatcher_fd_cbs(char *data_flow_group,stk_options_t *opts,
		stk_data_flow_fd_created_cb created_cb, stk_data_flow_fd_destroyed_cb destroyed_cb);
	stk_options_t *stk_option_append_dispatcher_fd_cbs(char *data_flow_group,stk_options_t *opts) {
		return stk_append_dispatcher_fd_cbs(data_flow_group,opts,NULL,NULL);
	}
}
