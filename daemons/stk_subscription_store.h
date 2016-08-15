#ifndef STK_SUBSCRIPTION_STORE_H
#define STK_SUBSCRIPTION_STORE_H
#include "stk_internal.h"
#include "stk_data_flow.h"
#include "PLists.h"

typedef struct {
	char name[STK_MAX_NAME_LEN];
	char group_name[STK_MAX_GROUP_NAME_LEN];
	struct timeval expiration_tv;
	stk_data_flow_t *df;
	stk_uint64 subscription_id;
} stk_name_subscription_data_t;

typedef struct stk_subscription_store_stct stk_subscription_store_t;
typedef stk_ret (*stk_subscription_store_cb)(stk_subscription_store_t *,void *);

stk_ret stk_subscription_store_init();
void stk_subscription_store_cleanup();
stk_ret stk_add_to_subscription_store(stk_subscription_store_t *store,stk_name_subscription_data_t *request_data);
stk_ret stk_remove_node_from_subscription_store(stk_subscription_store_t *store,Node *n,void **data);
Node *stk_find_subscription_in_store(stk_subscription_store_t *store,char *name,Node *last);
Node *stk_find_expired_subscriptions_in_store(stk_subscription_store_t *store,struct timeval *tv,Node *nxt);
Node *stk_find_subscriptions_from_df_in_store(stk_subscription_store_t *store,stk_data_flow_t *df,Node *nxt);
stk_subscription_store_t *stk_find_subscription_store(char *group_name);
stk_subscription_store_t *stk_default_subscription_store();
stk_subscription_store_t *stk_add_subscription_store(char *group_name);
Node *stk_first_subscription_in_store(stk_subscription_store_t *store);
Node *stk_next_subscription_in_store(stk_subscription_store_t *store,Node *n);
stk_ret stk_iterate_subscription_stores(stk_subscription_store_cb cb,void *clientd);
stk_uint64 stk_get_subscription_id(stk_name_subscription_data_t *subscription);
stk_data_flow_t *stk_get_subscription_data_flow(stk_name_subscription_data_t *subscription);

#endif
