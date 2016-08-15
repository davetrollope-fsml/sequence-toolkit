#ifndef STK_NAME_STORE_H
#define STK_NAME_STORE_H
#include "stk_internal.h"
#include "stk_data_flow.h"
#include "PLists.h"

typedef struct {
	char name[STK_MAX_NAME_LEN];
	char group_name[STK_MAX_GROUP_NAME_LEN];
	struct sockaddr_in ipv4;
	stk_sequence_t *meta_data;
	struct timeval expiration_tv;
	int linger;
	stk_data_flow_t *df;
	stk_uint64 request_id;
	stk_name_ft_state_t ft_state; /* Added to support future requests for name by state */
} stk_name_request_data_t;

typedef struct stk_name_store_stct stk_name_store_t;
typedef stk_ret (*stk_name_store_cb)(stk_name_store_t *,void *);

stk_ret stk_name_store_init();
void stk_name_store_cleanup();
stk_ret stk_add_to_name_store(stk_name_store_t *store,stk_name_request_data_t *request_data);
stk_ret stk_remove_node_from_name_store(stk_name_store_t *store,Node *n,void **data);
Node *stk_find_name_in_store(stk_name_store_t *store,char *name,Node *last);
Node *stk_find_expired_names_in_store(stk_name_store_t *store,struct timeval *tv,Node *nxt);
Node *stk_find_names_from_df_in_store(stk_name_store_t *store,stk_data_flow_t *df,Node *nxt);
stk_name_store_t *stk_find_name_store(char *group_name);
stk_name_store_t *stk_default_name_store();
stk_name_store_t *stk_add_name_store(char *group_name);
Node *stk_first_name_in_store(stk_name_store_t *store);
Node *stk_next_name_in_store(stk_name_store_t *store,Node *n);
stk_ret stk_iterate_name_stores(stk_name_store_cb cb,void *clientd);

#endif
