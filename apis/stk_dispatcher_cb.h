
extern "C" {
#include "../include/stk_options_api.h"
#include "../include/stk_env_api.h"
#include "../include/stk_service_group_api.h"
#include "../include/stk_service_group.h"
#include "../include/stk_service_api.h"
#include "../include/stk_service.h"
#include "../include/stk_data_flow_api.h"
#include "../include/stk_data_flow.h"
#include "../include/stk_sequence_api.h"
#include "../include/stk_sequence.h"
#include "../include/stk_tcp_server_api.h"
#include "../include/stk_timer_api.h"
#include "../examples/eg_dispatcher_api.h"
#include "../lib/stk_internal.h"
}

#define DEFAULT_IDLE_TIME 100

class stk_dispatch_cb_class {
public:
virtual ~stk_dispatch_cb_class() { STK_DEBUG(STKA_BIND,"~stk_dispatch_cb_class()"); }
virtual void process_data(unsigned long dfptr,unsigned long seqid) { STK_DEBUG(STKA_BIND,"stk_dispatch_cb_class::process_data()"); }
virtual void fd_created(unsigned long dfptr,int fd) { STK_DEBUG(STKA_BIND,"stk_dispatch_cb_class::fd_created()"); }
virtual void fd_destroyed(unsigned long dfptr,int fd) { STK_DEBUG(STKA_BIND,"stk_dispatch_cb_class::fd_destroyed()"); }
virtual void process_name_response(unsigned long dfptr,unsigned long seqid) { STK_DEBUG(STKA_BIND,"stk_dispatch_cb_class::process_name_response()"); }
virtual void process_monitoring_response(unsigned long dfptr,unsigned long seqid) { STK_DEBUG(STKA_BIND,"stk_dispatch_cb_class::process_monitoring_response()"); }
};

extern "C" {
void stk_process_data_cb(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_dispatch_cb_class *cb = (stk_dispatch_cb_class *) stk_get_dispatcher_user_data(d);
	STK_DEBUG(STKA_BIND,"stk_process_data_cb df %p calling upper API seq type %u seq id %lu",
		rcvchannel,stk_get_sequence_type(rcv_seq),stk_get_sequence_id(rcv_seq));

	cb->process_data((unsigned long) rcvchannel,(unsigned long) rcv_seq);
	STK_DEBUG(STKA_BIND,"stk_process_data_cb SUCCESS!");
}

void stk_process_name_response_cb(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_dispatch_cb_class *cb = (stk_dispatch_cb_class *) stk_get_dispatcher_user_data(d);
	STK_DEBUG(STKA_BIND,"stk_process_name_response_cb df %p calling upper API seq type %u seq id %lu",
		rcvchannel,stk_get_sequence_type(rcv_seq),stk_get_sequence_id(rcv_seq));

	cb->process_name_response((unsigned long) rcvchannel,(unsigned long) rcv_seq);
	STK_DEBUG(STKA_BIND,"stk_process_name_response_cb SUCCESS!");
}

void stk_process_monitoring_response_cb(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_dispatch_cb_class *cb = (stk_dispatch_cb_class *) stk_get_dispatcher_user_data(d);
	STK_DEBUG(STKA_BIND,"stk_process_monitoring_response_cb df %p calling upper API seq type %u seq id %lu",
		rcvchannel,stk_get_sequence_type(rcv_seq),stk_get_sequence_id(rcv_seq));

	cb->process_monitoring_response((unsigned long) rcvchannel,(unsigned long) rcv_seq);
	STK_DEBUG(STKA_BIND,"stk_process_monitoring_response_cb SUCCESS!");
}
}

class stk_dispatch_cb_caller {
private:
	stk_dispatch_cb_class *_callback;
	stk_dispatcher_t *dispatcher;

public:
	stk_dispatch_cb_caller(): _callback(0) {
		dispatcher = alloc_dispatcher();
		timer_dispatch_set = NULL;
		STK_DEBUG(STKA_BIND,"New dispatcher: %p",dispatcher);
	}
	unsigned long get_dispatcher() { return (unsigned long) dispatcher; }
	void detach_env(stk_env_t *stkbase) {
		if(timer_dispatch_set) {
			STK_ASSERT(STKA_BIND,stk_free_timer_set(timer_dispatch_set,STK_FALSE) == STK_SUCCESS,"free timer set");
			timer_dispatch_set = NULL;
		}
	}
	void close() {
		STK_DEBUG(STKA_BIND,"Close dispatcher: %p %p",dispatcher,timer_dispatch_set);

		if(dispatcher) {
			free_dispatcher(dispatcher);
			dispatcher = NULL;
			delCallback();
		}
	}
	~stk_dispatch_cb_caller() {
	}
	void delCallback() { delete _callback; _callback = 0; }

	static void end_client_dispatch_timer(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type)
	{
		stop_dispatching((stk_dispatcher_t *) userdata);
	}

	stk_timer_set_t *timer_dispatch_set;
	int env_listening_dispatcher_add_fd(stk_data_flow_t *df) {
		stk_dispatcher_t *d = dispatcher;
		return server_dispatch_add_fd(d,stk_tcp_server_fd(df),df,stk_process_data_cb);
	}
	void env_listening_dispatcher_del_fd(stk_data_flow_t *df) {
		stk_dispatcher_t *d = dispatcher;
		int removed = dispatch_remove_fd(d,stk_tcp_server_fd(df));
		STK_ASSERT(STKA_BIND,removed != -1,"remove data flow from dispatcher");
	}
	void env_listening_dispatcher(stk_data_flow_t *df,stk_dispatch_cb_class *cb,int ms) {
		stk_dispatcher_t *d = dispatcher;
		stk_env_t *stkbase = stk_env_from_data_flow(df);

		if(!timer_dispatch_set) timer_dispatch_set = stk_new_timer_set(stkbase,NULL,1,STK_TRUE);

		stk_set_dispatcher_user_data(d,cb);

		stk_schedule_timer(timer_dispatch_set,end_client_dispatch_timer,0,d,ms);

		eg_dispatcher(d,stkbase,DEFAULT_IDLE_TIME);
	}

	void env_client_dispatcher_timed(stk_env_t *env,int timeout,stk_dispatch_cb_class *cb) {
		stk_set_dispatcher_user_data(dispatcher,cb);
		client_dispatcher_timed(dispatcher,env,NULL,timeout);
	}

	void env_stop_dispatching(stk_env_t *env) {
		stop_dispatching(dispatcher);
		wakeup_dispatcher(env);
	}

	void env_terminate_dispatcher(stk_env_t *env) {
		terminate_dispatcher(dispatcher);
	}
};
