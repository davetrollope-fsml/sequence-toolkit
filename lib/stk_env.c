#include "stk_env_api.h"
#include "stk_env.h"
#include "stk_internal.h"
#include "stk_common.h"
#include "stk_timer.h"
#include "stk_options_api.h"
#include "stk_smartbeat_api.h"
#include "stk_timer_api.h"
#include "stk_name_service_api.h"
#include "stk_data_flow_api.h"
#include "stk_tcp_client_api.h"
#include "stk_udp_client_api.h"
#include <limits.h>
#include <string.h>
#include <ctype.h>

#define MAX_TIMER_POOL_SZ 25
struct stk_env_stct 
{
	stk_stct_type stct_type;
	stk_timer_set_t *timer_pool[MAX_TIMER_POOL_SZ];
	stk_wakeup_dispatcher_cb wakeup_cb;
	stk_smartbeat_ctrl_t *smb;
	stk_name_service_t *name_svc;
	stk_data_flow_t *monitoring_df;
	void *dispatcher;
};

stk_env_t *stk_create_env(stk_options_t *options)
{
	stk_env_t * env;
	stk_ret ret;

	STK_CALLOC_STCT(STK_STCT_ENV,stk_env_t,env);
	env->wakeup_cb = (stk_wakeup_dispatcher_cb) stk_find_option(options,"wakeup_cb",NULL);
	env->dispatcher = stk_find_option(options,"dispatcher",NULL);

	env->smb = stk_create_smartbeat_ctrl(env);
	STK_ASSERT(STK_STCT_ENV,env->smb!=NULL,"create smartbeat controller");

	stk_options_t *inhibit_name_svc = stk_find_option(options,"inhibit_name_service",NULL);
	if(!inhibit_name_svc) {
		stk_options_t *name_server_options = stk_find_option(options,"name_server_options",NULL);

		env->name_svc = stk_create_name_service(env, name_server_options ? name_server_options : options);
		STK_ASSERT(STKA_MEM,env->name_svc!=NULL,"configure the name server");
	}

	ret = stk_set_env_monitoring_data_flow(env,options);
	STK_ASSERT(STKA_DF,ret==STK_SUCCESS,"create monitoring data flow");

	return env;
}

void stk_reap_monitoring_cb(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type)
{
	stk_ret rc = stk_destroy_data_flow(userdata);
	STK_ASSERT(STKA_MEM,rc == STK_SUCCESS,"destroy monitoring data flow %p",userdata);
}

stk_ret stk_set_env_monitoring_data_flow(stk_env_t *env,stk_options_t *options)
{
	/* Process monitoring_data_flow_options, open monitoring df, store df for other modules */
	{
	stk_create_data_flow_t monitoring_create_df = stk_tcp_client_create_data_flow;

	/* Determine if the monitoring data flow protocol is configured */
	char *optstr = stk_find_option(options,"monitoring_data_flow_protocol",NULL);
	if(optstr && !strcasecmp(optstr,"udp"))
		monitoring_create_df = stk_udp_client_create_data_flow;

	/* Service monitoring is determined by the configuration of a monitoring data flow */
	{
	stk_data_flow_t *df = stk_data_flow_process_extended_options(env,options,"monitoring_data_flow",monitoring_create_df);
	if(df && env->monitoring_df) {
		/* Schedule current monitoring_df to be reaped later 
		 * callback for stk_data_flow_destroy(env->monitoring_df)
		 */
		stk_timer_t *reap_timer = stk_schedule_timer(env->timer_pool[0],stk_reap_monitoring_cb,0,env->monitoring_df,60000);
		STK_ASSERT(STKA_DF,reap_timer!=NULL,"schedule monitoring reaper timer for data flow %p",env->monitoring_df);
	}
	env->monitoring_df = df;
	}

	}
	return STK_SUCCESS;
}

stk_bool stk_destroy_env(stk_env_t *env)
{
	if(env->monitoring_df) {
		stk_ret rc = stk_destroy_data_flow(env->monitoring_df);
		STK_ASSERT(STKA_MEM,rc == STK_SUCCESS,"destroy monitoring data flow");
	}

	if(env->name_svc) {
		stk_ret rc = stk_destroy_name_service(env->name_svc);
		STK_ASSERT(STKA_MEM,rc == STK_SUCCESS,"destroy name service");
	}

	if(env->smb) {
		stk_ret rc = stk_destroy_smartbeat_ctrl(env->smb);
		STK_ASSERT(STKA_MEM,rc == STK_SUCCESS,"destroy smartbeat controller");
	}

	for(int idx = 0; idx < MAX_TIMER_POOL_SZ; idx++) {
		STK_ASSERT(STKA_MEM,env->timer_pool[idx] == NULL,"Timer pool %d not freed when closing env",idx);
	}

	STK_FREE_STCT(STK_STCT_ENV,env);
	return STK_SUCCESS;
}

stk_ret stk_env_add_timer_set(stk_env_t *env,stk_timer_set_t *tset)
{
	for(int idx = 0; idx < MAX_TIMER_POOL_SZ; idx++) {
		if(env->timer_pool[idx] == NULL) {
			env->timer_pool[idx] = tset;
			return STK_SUCCESS;
		}
	}
	return !STK_SUCCESS;
}

stk_ret stk_env_remove_timer_set(stk_env_t *env,stk_timer_set_t *tset)
{
	for(int idx = 0; idx < MAX_TIMER_POOL_SZ; idx++) {
		if(env->timer_pool[idx] == tset) {
			env->timer_pool[idx] = NULL;
			return STK_SUCCESS;
		}
	}
	return !STK_SUCCESS;
}

stk_ret stk_env_dispatch_timer_pool(stk_env_t *env,unsigned short max_callbacks,int idx)
{
	if(env->timer_pool[idx])
		return stk_dispatch_timers(env->timer_pool[idx],max_callbacks);
	return !STK_SUCCESS;
}

stk_ret stk_env_dispatch_timer_pools(stk_env_t *env,unsigned short max_callbacks)
{
	stk_ret rc;

	for(int idx = 0; idx < MAX_TIMER_POOL_SZ; idx++) {
		if(env->timer_pool[idx]) {
			rc = stk_env_dispatch_timer_pool(env,max_callbacks,idx);
			if(rc != STK_SUCCESS) return rc;
		}
	}
	return STK_SUCCESS;
}

int stk_next_timer_ms_in_pool(stk_env_t *env)
{
	int mintime = INT_MAX;

	for(int idx = 0; idx < MAX_TIMER_POOL_SZ; idx++) {
		if(env->timer_pool[idx]) {
			int nxt_timer = stk_next_timer_ms(env->timer_pool[idx]);
			if(nxt_timer != -1 && nxt_timer < mintime) mintime = nxt_timer;
		}
	}
	return mintime;
}

void stk_wakeup_dispatcher(stk_env_t *env) { if(env->wakeup_cb) env->wakeup_cb(env); }

void *stk_env_get_dispatcher(stk_env_t *env) { return env->dispatcher; }

stk_timer_set_t *stk_env_get_timer_set(stk_env_t *env,int pool_idx) { return env->timer_pool[pool_idx]; }

stk_smartbeat_ctrl_t *stk_env_get_smartbeat_ctrl(stk_env_t *env) { return env->smb; }

stk_name_service_t *stk_env_get_name_service(stk_env_t *env) { return env->name_svc; }

stk_data_flow_t *stk_env_get_monitoring_data_flow(stk_env_t *env) { return env->monitoring_df; }


/* Assert functions */
FILE *stk_assert_log_file;
int stk_assert_log = 0;
unsigned int stk_assert_val = ~0;
int stk_assert_flush = 1;
int stk_stderr_level = STK_LOG_NORMAL;

struct {
	unsigned int mask;
	char *name;
} stka_name_map[] = {
	STKA_NAME_MAP
};

void stk_assert_init() {
	char *env = getenv("ASSERT_LOG");
	if(env) {
		char *comma = strchr(env,',');
		if(comma) {
			char *file = comma + 1;
			char *opts = strchr(file,',');
			*comma = '\0'; /* HACK! */
			if(opts) {
				*opts = '\0'; /* HACK! */
				if(strcmp(++opts,"noflush") == 0)
					stk_assert_flush = 0;
			}
			stk_assert_log_file = fopen(file,"w");
		} else
			stk_assert_log_file = stderr;

		if(strncmp(env,"0x",2) == 0) {
			long int val = strtol(&env[2],NULL,16);
			stk_assert_val = val;
		} else {
			if(isdigit(env[0]))
				stk_assert_val = atoi(env);
			else {
				/* map names to value */
				char *names = env;
				char *plus;
				int i;
				stk_assert_val = 0;
				do {
					plus = names;
					plus = strchr(plus,'+');
					if(plus) *plus = '\0'; /* HACK! */
					/* find name in map */
					for(i = 0; stka_name_map[i].name && strcasecmp(stka_name_map[i].name,names) != 0; ) i++;
					if(stka_name_map[i].name)
						stk_assert_val |= stka_name_map[i].mask;
					else
						printf("%s not found\n",names);
					if(plus) {
						names = plus + 1;
						continue;
					} else
						break;
				} while(1);
			}
		}
		stk_assert_log = 1;
	}
	else
		stk_assert_log = -1;
}

void stk_set_stderr_level(int level)
{
	stk_stderr_level = level;
}
