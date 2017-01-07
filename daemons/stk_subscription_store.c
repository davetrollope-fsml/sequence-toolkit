
#include "stk_internal.h"
#include "stk_name_service.h"
#include "stk_subscription_store.h"
#include "stk_sequence_api.h"
#include "PLists.h"
#include <ctype.h>
#include <string.h>

struct stk_subscription_store_stct {
	char group_name[STK_MAX_GROUP_NAME_LEN];
	List *subscription_list[STK_MAX_NAME_LIST_ENTRIES]; /* Array of lists based on first char of name */
};
static List *store_groups;
static stk_subscription_store_t *default_store;

stk_subscription_store_t *stk_alloc_subscription_store(char *group_name)
{
	stk_subscription_store_t *store = STK_CALLOC(sizeof(stk_subscription_store_t));
	if(!store) {
		STK_DEBUG(STKA_NS,"failed to alloc store %s",group_name);
		return NULL;
	}

	if(group_name)
		strcpy(store->group_name,group_name);

	for(unsigned int i = 0; i < sizeof(store->subscription_list)/sizeof(List *);i ++) {
		store->subscription_list[i] = NewPList();
		if(!store->subscription_list[i]) {
			for(; i < sizeof(store->subscription_list)/sizeof(store->subscription_list[0]) ;i --)
				FreeList(store->subscription_list[i]);
			return NULL;
		}
	}
	return store;
}

void stk_free_subscription_store(stk_subscription_store_t *store)
{
	for(unsigned int i = 0; i < sizeof(store->subscription_list)/sizeof(List *);i ++) {
		FreeList(store->subscription_list[i]);
	}
	STK_FREE(store);
}

Node *stk_alloc_subscription_store_node(char *group_name)
{
	Node *n;
	stk_subscription_store_t *store = stk_alloc_subscription_store(group_name);
	STK_ASSERT(STKA_NS,store!=NULL,"alloc store %s",group_name);

	n = NewNode();
	STK_ASSERT(STKA_NS,n!=NULL,"alloc default store node");
	SetData(n,store);
	return n;
}

stk_subscription_store_t *stk_add_subscription_store(char *group_name)
{
	Node *n = stk_alloc_subscription_store_node(group_name);
	if(!n) return NULL;

	AddTail(store_groups,n);

	return NodeData(n);
}

stk_ret stk_subscription_store_init()
{
	Node *n;
	stk_ret rc;

	store_groups = NewPList();
	STK_ASSERT(STKA_NS,store_groups!=NULL,"alloc store group list");

	default_store = stk_add_subscription_store(NULL);
	STK_ASSERT(STKA_NS,default_store!=NULL,"alloc default store");

	return STK_SUCCESS;
}

void stk_subscription_store_cleanup()
{
	if(store_groups) {
		/* Free groups? */
		for(; !IsPListEmpty(store_groups); ) {
			stk_subscription_store_t *store;
			Node *n = FirstNode(store_groups);
			stk_ret rc = stk_remove_node_from_subscription_store((stk_subscription_store_t *)NodeData(n),n,(void **)&store);
			stk_free_subscription_store(store);
			STK_ASSERT(STKA_NS,rc == STK_SUCCESS,"removing and freeing store");
		}
		FreeList(store_groups);
	}
}

static unsigned char stk_determine_bucket(char *name)
{
	unsigned char bucket = (unsigned char) (toupper(name[0]) - 'A');
	if(bucket > STK_MAX_NAME_LIST_ENTRIES) bucket = STK_MAX_NAME_LIST_ENTRIES - 1;
	return bucket;
}

stk_ret stk_add_to_subscription_store(stk_subscription_store_t *store,stk_name_subscription_data_t *request_data)
{
	unsigned char bucket = stk_determine_bucket(request_data->name);
	STK_DEBUG(STKA_NS,"adding name %s to bucket %d store %p",request_data->name,bucket,store);

	if(!store) store = default_store;

	Node *n = NewNode(); /* Allocs cleared data */
	STK_ASSERT(STKA_NS,n!=NULL,"alloc name node to add to list for name %s",request_data->name);

	SetData(n,request_data);
	AddTail(store->subscription_list[bucket],n);

	return STK_SUCCESS;
}

stk_ret stk_remove_node_from_subscription_store(stk_subscription_store_t *store,Node *n,void **data)
{
	Remove(n);
	if(data)
		*data = NodeData(n);
	SetData(n,NULL);
	FreeNode(n);
	return STK_SUCCESS;
}

Node *stk_find_subscription_in_store(stk_subscription_store_t *store,char *name,Node *last)
{
	Node *start;
	unsigned char bucket = stk_determine_bucket(name);
	STK_DEBUG(STKA_NS,"searching for name %s in bucket %d store %p",name,bucket,store);

	if(!store) store = default_store;

	start = last ? NxtNode(last) : FirstNode(store->subscription_list[bucket]);
	for(Node *n = start; !AtListEnd(n); n = NxtNode(n)) {
		if(strcmp(((stk_name_subscription_data_t *)NodeData(n))->name,name) == 0)
			return n;
	}
	return NULL;
}

Node *stk_find_expired_subscriptions_in_store(stk_subscription_store_t *store,struct timeval *tv,Node *nxt)
{
	Node *start;

	if(!store) store = default_store;

	start = nxt ? nxt : stk_first_subscription_in_store(store);
	STK_DEBUG(STKA_NS,"find_expired_subscriptions_in_store %p %p %p",store,nxt,start);
	for(Node *n = start; n; n = stk_next_subscription_in_store(store,n)) {
		stk_name_subscription_data_t *request = (stk_name_subscription_data_t *)NodeData(n);

		STK_DEBUG(STKA_NS,"checking time of node %p req %p name %s %ld.%d vs %ld.%d",
			n,request,request->name,
			tv->tv_sec,tv->tv_usec,
			request->expiration_tv.tv_sec,request->expiration_tv.tv_usec);

		if(request->expiration_tv.tv_sec == 0 && request->expiration_tv.tv_usec == 0)
			continue;

		if(timercmp(tv,&request->expiration_tv,>)) {
			STK_DEBUG(STKA_NS,"store %p expired %p %s",store,n,request->name);
			return n;
		}
	}
	return NULL;
}

Node *stk_find_subscriptions_from_df_in_store(stk_subscription_store_t *store,stk_data_flow_t *df,Node *nxt)
{
	Node *start;

	if(!store) store = default_store;

	start = nxt ? nxt : stk_first_subscription_in_store(store);
	STK_DEBUG(STKA_NS,"find_subscriptions_from_df_in_store %p %p %p %p",store,nxt,start,df);
	for(Node *n = start; n; n = stk_next_subscription_in_store(store,n)) {
		stk_name_subscription_data_t *request = (stk_name_subscription_data_t *)NodeData(n);

		STK_DEBUG(STKA_NS,"find_subscriptions_from_df_in_store %p %p",request->df,df);
		if(request->df == df) {
			STK_DEBUG(STKA_NS,"store %p lingering %p %s",store,n,request->name);
			return n;
		}
	}
	return NULL;
}

stk_subscription_store_t *stk_find_subscription_store(char *group_name)
{
	if(!group_name || group_name[0] == '\0') return default_store;

	STK_DEBUG(STKA_NS,"searching for store group name %s",group_name);

	for(Node *n = FirstNode(store_groups); !AtListEnd(n); n = NxtNode(n)) {
		if(strcmp(((stk_subscription_store_t *)NodeData(n))->group_name,group_name) == 0)
			return NodeData(n);
	}

	return default_store;
}

stk_ret stk_iterate_subscription_stores(stk_subscription_store_cb cb,void *clientd)
{
	stk_ret rc;

	STK_DEBUG(STKA_NS,"iterating store groups %p",cb);

	for(Node *n = FirstNode(store_groups); !AtListEnd(n); n = NxtNode(n)) {
		rc = cb((stk_subscription_store_t *)NodeData(n),clientd);
		if(rc != STK_SUCCESS) return rc;
	}
	rc = cb((stk_subscription_store_t *)default_store,clientd);
	if(rc != STK_SUCCESS) return rc;

	return STK_SUCCESS;
}

Node *stk_first_subscription_in_store(stk_subscription_store_t *store)
{
	int bucket = 0;

	if(!store) store = default_store;

	while(bucket < 27 && IsPListEmpty(store->subscription_list[bucket])) bucket++;
	return bucket > 26 ? NULL : FirstNode(store->subscription_list[bucket]);
}

Node *stk_next_subscription_in_store(stk_subscription_store_t *store,Node *n)
{
	Node *nxt = NxtNode(n);

	if(!store) store = default_store;

	if(AtListEnd(nxt)) {
		/* Figure out which bucket this is in and return the first node of the next bucket that isn't empty */
		List *l = ListFromLastNode(n);
		int bucket = 0;

		while(bucket < 27 && l != store->subscription_list[bucket]) bucket++;
		bucket++;
		while(bucket < 27 && IsPListEmpty(store->subscription_list[bucket])) bucket++;
		nxt = bucket > 26 ? NULL : FirstNode(store->subscription_list[bucket]);
	}
	return nxt;
}

stk_subscription_store_t *stk_default_subscription_store() { return default_store; }

stk_uint64 stk_get_subscription_id(stk_name_subscription_data_t *subscription) { return subscription->subscription_id; }
stk_data_flow_t *stk_get_subscription_data_flow(stk_name_subscription_data_t *subscription) { return subscription->df; }

