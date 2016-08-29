

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

class stk_name_service_cb_class {
public:
virtual ~stk_name_service_cb_class() { STK_DEBUG(STKA_BIND,"~stk_name_service_cb_class()"); }
/* These should be converted to pass ulongs and reduce the param count as occurs with sequence and df cbs */
virtual void name_service_info_cb(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info,stk_name_info_cb_type cb_type) { STK_DEBUG(STKA_BIND,"stk_name_service_cb_class.name_service_info_cb()"); }
};

class stk_name_service_cb_caller {
private:
	stk_name_service_cb_class *_callback;
	stk_service_group_t *_svcgrp;
public:
	stk_name_service_cb_caller(): _callback(0) {}
	~stk_name_service_cb_caller() { delCallback(); }
	void delCallback() { delete _callback; _callback = 0; }

	static void stk_name_service_info_cb(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info,stk_name_info_cb_type cb_type)
	{
		stk_options_t *options;

		STK_DEBUG(STKA_BIND,"stk_service_name_info_cb name_info %p app_info %p cb_type %d",name_info,app_info,cb_type);
		stk_name_service_cb_class *cb = (stk_name_service_cb_class *) app_info;
		STK_DEBUG(STKA_BIND,"stk_name_service_info_cb calling upper API name_service_info()");
		cb->name_service_info_cb(name_info,name_count,server_info,NULL,cb_type);
		STK_DEBUG(STKA_BIND,"stk_name_service_info_cb SUCCESS!");
	}

	stk_options_t *stk_append_name_service_cb(stk_options_t *opts,stk_name_service_cb_class *cb) {
		stk_options_t *e = stk_copy_extend_options(opts, 2);
		stk_append_option(e,strdup("name_service_info_cb"),(void*) stk_name_service_info_cb);
		stk_append_option(e,strdup("name_service_info_cb_data"),(void*) cb);
		return e;
	}
	void stk_remove_name_service_cb(stk_options_t *e) {
		stk_update_option(e,(char *) "name_service_info_cb",(void*) NULL,NULL);
		stk_update_option(e,(char *) "name_service_info_cb_data",(void*) NULL,NULL);
	}

	stk_ret stk_register_name_cls(stk_name_service_t *named, char *name, int linger, int expiration_ms, stk_name_service_cb_class *cls, void *app_info, stk_options_t *options)
	{
		STK_DEBUG(STKA_BIND,"stk_register_name_cls name %s app_info %p", name, app_info);

		return stk_register_name(named,name,linger,expiration_ms,stk_name_service_info_cb,cls,options);
	}

	stk_ret stk_request_name_info_cls(stk_name_service_t *named, char *name, int expiration_ms, stk_name_service_cb_class *cls, void *app_info, stk_options_t *options)
	{
		return stk_request_name_info(named,name,expiration_ms,stk_name_service_info_cb,cls,options);
	}

	stk_ret stk_subscribe_name_info_cls(stk_name_service_t *named, char *name, stk_name_service_cb_class *cls, void *app_info, stk_options_t *options)
	{
		return stk_subscribe_to_name_info(named,name,stk_name_service_info_cb,cls,options);
	}
};
