

extern "C" {
#include "../include/stk_options_api.h"
#include "../include/stk_env_api.h"
#include "../include/stk_service_group_api.h"
#include "../include/stk_service_group.h"
#include "../include/stk_service_api.h"
#include "../include/stk_service.h"
#include "../include/stk_smartbeat.h"
#include "../lib/stk_internal.h"
}

class stk_service_cb_class {
public:
virtual ~stk_service_cb_class() { STK_DEBUG(STKA_BIND,"Callback::~Callback()"); }
/* These should be converted to pass ulongs and reduce the param count as occurs with sequence and df cbs */
virtual void added(stk_service_group_id svcgrpid,stk_service_id svcid,short state,stk_service_t *svc) { }
virtual void removed(stk_service_group_id svcgrpid,stk_service_id svcid,short state) { }
virtual void state_chg(stk_service_id svcid,stk_service_state old_state,stk_service_state new_state) { }
virtual void smartbeat(stk_service_group_id svcgrpid,stk_service_id svcid,stk_smartbeat_t *smartbeat) { }
};

class stk_service_cb_caller {
private:
	stk_service_cb_class *_callback;
	stk_service_group_t *_svcgrp;
public:
	stk_service_cb_caller(): _callback(0) {}
	~stk_service_cb_caller() { delCallback(); }
	void delCallback() { delete _callback; _callback = 0; }

	static void stk_service_added_cb(stk_service_group_t *svc_group, stk_service_t *svc,stk_service_in_group_state state)
	{
		stk_options_t *options;

		STK_DEBUG(STKA_BIND,"stk_service_added_cb svc_group %p svc %p state %d",svc_group,svc,state);

		options = stk_get_service_group_options(svc_group);
		if(!options) { STK_DEBUG(STKA_BIND,"stk_service_added_cb return no options"); return; }

		void *svc_added_userdata = (void *) stk_find_option(options, (char *)"service_added_cb_data",NULL);
		if(!svc_added_userdata) { STK_DEBUG(STKA_BIND,"stk_service_added_cb return no userdata"); return; }

		stk_service_cb_class *cb = (stk_service_cb_class *) svc_added_userdata;
		STK_DEBUG(STKA_BIND,"stk_service_added_cb calling upper API added()");
		cb->added(stk_get_service_group_id(svc_group),stk_get_service_id(svc),state,svc);
		STK_DEBUG(STKA_BIND,"stk_service_added_cb SUCCESS!");
	}

	static void stk_service_removed_cb(stk_service_group_t *svc_group, stk_service_t *svc,stk_service_in_group_state state)
	{
		stk_options_t *options;

		STK_DEBUG(STKA_BIND,"stk_service_removed_cb svc_group %p svc %p state %d",svc_group,svc,state);

		options = stk_get_service_group_options(svc_group);
		if(!options) { STK_DEBUG(STKA_BIND,"stk_service_removed_cb return no options"); return; }

		void *svc_removed_userdata = (void *) stk_find_option(options, (char *)"service_removed_cb_data",NULL);
		if(!svc_removed_userdata) { STK_DEBUG(STKA_BIND,"stk_service_removed_cb return no userdata"); return; }

		stk_service_cb_class *cb = (stk_service_cb_class *) svc_removed_userdata;
		cb->removed(stk_get_service_group_id(svc_group),stk_get_service_id(svc),state);
	}

	static void stk_state_change_cb(stk_service_t *svc,stk_service_state old_state,stk_service_state new_state)
	{
		stk_options_t *options;

		STK_DEBUG(STKA_BIND,"stk_state_change_cb svc %p old_state %d new_state %d",svc,old_state,new_state);

		options = stk_get_service_options(svc);
		if(!options) { STK_DEBUG(STKA_BIND,"stk_state_change_cb return no options"); return; }

		void *svc_state_chg_userdata = (void *) stk_find_option(options, (char *)"state_change_cb_data",NULL);
		if(!svc_state_chg_userdata) { STK_DEBUG(STKA_BIND,"stk_state_change_cb return no userdata"); return; }

		stk_service_cb_class *cb = (stk_service_cb_class *) svc_state_chg_userdata;
		cb->state_chg(stk_get_service_id(svc),old_state,new_state);
	}

	static void stk_service_smartbeat_cb(stk_service_group_t *svc_group, stk_service_t *svc,stk_smartbeat_t *smartbeat)
	{
		stk_options_t *options;

		STK_DEBUG(STKA_BIND,"stk_service_smartbeat_cb svc_group %p svc %p smartbeat %p",svc_group,svc,smartbeat);

		options = stk_get_service_group_options(svc_group);
		if(!options) { STK_DEBUG(STKA_BIND,"stk_service_smartbeat_cb return no options"); return; }

		void *svc_smartbeat_userdata = (void *) stk_find_option(options, (char *)"service_smartbeat_cb_data",NULL);
		if(!svc_smartbeat_userdata) { STK_DEBUG(STKA_BIND,"stk_service_smartbeat_cb return no userdata"); return; }

		stk_service_cb_class *cb = (stk_service_cb_class *) svc_smartbeat_userdata;
		cb->smartbeat(stk_get_service_group_id(svc_group),stk_get_service_id(svc),smartbeat);
	}

	stk_options_t *stk_append_service_cb(stk_options_t *opts,stk_service_cb_class *cb) {
		stk_options_t *e = stk_copy_extend_options(opts, 8);
		stk_append_option(e,strdup("service_added_cb"),(void*) stk_service_added_cb);
		stk_append_option(e,strdup("service_added_cb_data"),(void*) cb);
		stk_append_option(e,strdup("service_removed_cb"),(void*) stk_service_removed_cb);
		stk_append_option(e,strdup("service_removed_cb_data"),(void*) cb);
		stk_append_option(e,strdup("state_change_cb"),(void*) stk_state_change_cb);
		stk_append_option(e,strdup("state_change_cb_data"),(void*) cb);
		stk_append_option(e,strdup("service_smartbeat_cb"),(void*) stk_service_smartbeat_cb);
		stk_append_option(e,strdup("service_smartbeat_cb_data"),(void*) cb);
		return e;
	}
	void stk_remove_service_cb(stk_options_t *e) {
		stk_update_option(e,(char *) "service_added_cb",(void*) NULL,NULL);
		stk_update_option(e,(char *) "service_added_cb_data",(void*) NULL,NULL);
		stk_update_option(e,(char *) "service_removed_cb",(void*) NULL,NULL);
		stk_update_option(e,(char *) "service_removed_cb_data",(void*) NULL,NULL);
		stk_update_option(e,(char *) "state_change_cb",(void*) NULL,NULL);
		stk_update_option(e,(char *) "state_change_cb_data",(void*) NULL,NULL);
		stk_update_option(e,(char *) "service_smartbeat_cb",(void*) NULL,NULL);
		stk_update_option(e,(char *) "service_smartbeat_cb_data",(void*) NULL,NULL);
	}
};
