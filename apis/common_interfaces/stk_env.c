extern "C" {
#include "../../examples/eg_dispatcher_api.h"
	extern void stk_process_data_cb(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq);
	extern void stk_process_name_response_cb(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq);
	extern void stk_process_monitoring_response_cb(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq);
	void stk_fd_created_autocb(stk_data_flow_t *flow,stk_data_flow_id id,int fd) {
		stk_env_t *stkbase = stk_env_from_data_flow(flow);
		stk_dispatcher_t *dispatcher = (stk_dispatcher_t *) stk_env_get_dispatcher(stkbase);
		if(!dispatcher)
			dispatcher = default_dispatcher();

		if (stk_get_data_flow_type(flow) == STK_TCP_SERVER_FLOW) {
			int added = server_dispatch_add_fd(dispatcher,fd,flow,stk_process_data_cb);
			STK_ASSERT(STKA_NET,added != -1,"add server data flow (fd %d) to dispatcher",fd);
		} else if (stk_get_data_flow_type(flow) == STK_TCP_ACCEPTED_FLOW) {
			int added = dispatch_add_accepted_fd(dispatcher,fd,flow,stk_process_data_cb);
			STK_ASSERT(STKA_NET,added != -1,"add accepted data flow (fd %d) to dispatcher",fd);
		} else {
			int added = dispatch_add_fd(dispatcher,flow,fd,NULL,stk_process_data_cb);
			STK_ASSERT(STKA_NET,added != -1,"Failed to add data flow to dispatcher");
		}

		{
		stk_dispatch_cb_class *cb = (stk_dispatch_cb_class *) stk_get_dispatcher_user_data(dispatcher);
		STK_DEBUG(STKA_BIND,"stk_fd_created_autocb cb %p df %p calling upper API",cb,flow);
		if(cb)
			cb->fd_created((unsigned long) flow, fd);
		}
	}
	void stk_name_fd_created_autocb(stk_data_flow_t *flow,stk_data_flow_id id,int fd) {
		stk_env_t *stkbase = stk_env_from_data_flow(flow);
		void *dispatcher = (void *) stk_env_get_dispatcher(stkbase);
		int added = dispatch_add_fd((stk_dispatcher_t *) (dispatcher ? dispatcher : default_dispatcher()),flow,fd,NULL,stk_process_name_response_cb);
		STK_ASSERT(STKA_NET,added != -1,"Failed to add data flow to dispatcher");
	}
	void stk_monitoring_fd_created_autocb(stk_data_flow_t *flow,stk_data_flow_id id,int fd) {
		stk_env_t *stkbase = stk_env_from_data_flow(flow);
		void *dispatcher = (void *) stk_env_get_dispatcher(stkbase);
		int added = dispatch_add_fd((stk_dispatcher_t *) (dispatcher ? dispatcher : default_dispatcher()),flow,fd,NULL,stk_process_monitoring_response_cb);
		STK_ASSERT(STKA_NET,added != -1,"Failed to add data flow to dispatcher");
	}
	void stk_fd_destroyed_autocb(stk_data_flow_t *flow,stk_data_flow_id id,int fd) {
		stk_env_t *stkbase = stk_env_from_data_flow(flow);
		stk_dispatcher_t *dispatcher = (stk_dispatcher_t *) stk_env_get_dispatcher(stkbase);
		if(!dispatcher)
			dispatcher = default_dispatcher();

		int removed = dispatch_remove_fd(dispatcher,fd);
		STK_ASSERT(STKA_NET,removed != -1,"Failed to remove data flow from dispatcher");

		{
		stk_dispatch_cb_class *cb = (stk_dispatch_cb_class *) stk_get_dispatcher_user_data(dispatcher);
		STK_DEBUG(STKA_BIND,"stk_fd_destroyed_autocb cb %p df %p calling upper API",cb,flow);
		if(cb)
			cb->fd_destroyed((unsigned long) flow, fd);
		}
	}
	void stk_log(int level, char *message) { STK_LOG(level,"%s",message); }
	void stk_debug(int component, char *message) { STK_DEBUG(component,"%s",message); }
	stk_options_t *stk_append_dispatcher_fd_cbs(char *data_flow_group,stk_options_t *opts,
		stk_data_flow_fd_created_cb created_cb, stk_data_flow_fd_destroyed_cb destroyed_cb) {
		char data_flow_group_f[128];
		stk_options_t *g;
		if(data_flow_group == NULL)
			g = opts;
		else {
			strcpy(data_flow_group_f,data_flow_group);
			strcat(data_flow_group_f,"_options");

			g = (stk_options_t *) stk_find_option(opts, data_flow_group_f, NULL);
			if(!g) return opts;
		}

		{
		stk_options_t *e = stk_copy_extend_options(g, 2);
		stk_append_option(e,strdup("fd_created_cb"),(void*) (created_cb ? created_cb : stk_fd_created_autocb));
		stk_append_option(e,strdup("fd_destroyed_cb"),(void*) (destroyed_cb ? destroyed_cb : stk_fd_destroyed_autocb));
		/* Update original options because it may have been realloc'd */
		if(g != opts) {
			stk_update_option(opts,data_flow_group_f,e,NULL);
			return opts;
		}
		else
			return e;
		}
	}
	stk_options_t *stk_append_name_server_fd_cbs(char *data_flow_group,stk_options_t *opts) {
		return stk_append_dispatcher_fd_cbs(data_flow_group,opts,stk_name_fd_created_autocb,NULL);
	}
	stk_options_t *stk_append_monitoring_fd_cbs(char *data_flow_group,stk_options_t *opts) {
		return stk_append_dispatcher_fd_cbs(data_flow_group,opts,stk_name_fd_created_autocb,NULL);
	}
}
