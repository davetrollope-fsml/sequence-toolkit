
#include "stk_internal.h"
#include "stk_name_service.h"
#include "stk_name_store.h"
#include "stk_sequence_api.h"
#include "PLists.h"
#include <ctype.h>
#include <string.h>

#define MAX_LITE_NAME_COUNT 500

struct stk_name_store_stct {
	char group_name[STK_MAX_GROUP_NAME_LEN];
	List *name_list[27]; /* Array of lists based on first char of name */
};
static List *store_groups;
static stk_name_store_t *default_store;

stk_name_store_t *stk_alloc_name_store(char *group_name)
{
	stk_name_store_t *store = STK_CALLOC(sizeof(stk_name_store_t));
	if(!store) {
		STK_DEBUG(STKA_NS,"failed to alloc store %s",group_name);
		return NULL;
	}

	if(group_name)
		strcpy(store->group_name,group_name);

	for(unsigned int i = 0; i < sizeof(store->name_list)/sizeof(List *);i ++) {
		store->name_list[i] = NewPList();
		if(!store->name_list[i]) {
			for(; i < sizeof(store->name_list)/sizeof(store->name_list[0]) ;i --)
				FreeList(store->name_list[i]);
			return NULL;
		}
	}
	return store;
}

void stk_free_name_store(stk_name_store_t *store)
{
	for(unsigned int i = 0; i < sizeof(store->name_list)/sizeof(List *);i ++) {
		Node *start = FirstNode(store->name_list[i]);
		for(Node *n = start; !AtListEnd(n); n = NxtNode(n)) {
			stk_name_request_data_t *request_data = (stk_name_request_data_t *) NodeData(n);

			if(request_data->meta_data) {
				stk_destroy_sequence(request_data->meta_data);
				request_data->meta_data = NULL;
			}
		}

		FreeList(store->name_list[i]);
	}
	STK_FREE(store);
}

Node *stk_alloc_name_store_node(char *group_name)
{
	Node *n;
	stk_name_store_t *store = stk_alloc_name_store(group_name);
	STK_ASSERT(STKA_NS,store!=NULL,"alloc store %s",group_name);

	n = NewNode();
	STK_ASSERT(STKA_NS,n!=NULL,"alloc default store node");
	SetData(n,store);
	return n;
}

stk_name_store_t *stk_add_name_store(char *group_name)
{
	Node *n = stk_alloc_name_store_node(group_name);
	if(!n) return NULL;

	AddTail(store_groups,n);

	return NodeData(n);
}

stk_ret stk_name_store_init()
{
	Node *n;
	stk_ret rc;

	store_groups = NewPList();
	STK_ASSERT(STKA_NS,store_groups!=NULL,"alloc store group list");

	default_store = stk_add_name_store(NULL);
	STK_ASSERT(STKA_NS,default_store!=NULL,"alloc default store");

	return STK_SUCCESS;
}

void stk_name_store_cleanup()
{
	if(store_groups) {
		/* Free groups? */
		for(; !IsPListEmpty(store_groups); ) {
			stk_name_store_t *store;
			Node *n = FirstNode(store_groups);
			stk_ret rc = stk_remove_node_from_name_store((stk_name_store_t *)NodeData(n),n,(void **)&store);
			stk_free_name_store(store);
			STK_ASSERT(STKA_NS,rc == STK_SUCCESS,"removing and freeing store");
		}
		FreeList(store_groups);
	}
}

static unsigned char stk_determine_bucket(char *name)
{
	unsigned char bucket = (unsigned char) (toupper(name[0]) - 'A');
	if(bucket > 26) bucket = 27;
	return bucket;
}

static int name_count;
stk_ret stk_add_to_name_store(stk_name_store_t *store,stk_name_request_data_t *request_data)
{
	unsigned char bucket = stk_determine_bucket(request_data->name);
	STK_DEBUG(STKA_NS,"adding name %s to bucket %d store %p",request_data->name,bucket,store);

#ifdef STK_LITE
	if(name_count > MAX_LITE_NAME_COUNT) {
		STK_LOG(STK_LOG_NORMAL,"Exceeded name license, ignoring registration");
		return STK_NO_LICENSE;
	}
#endif

	if(!store) store = default_store;

	Node *n = NewNode(); /* Allocs cleared data */
	STK_ASSERT(STKA_NS,n!=NULL,"alloc name node to add to list for name %s",request_data->name);

	SetData(n,request_data);
	AddTail(store->name_list[bucket],n);

	name_count++;

	return STK_SUCCESS;
}

stk_ret stk_remove_node_from_name_store(stk_name_store_t *store,Node *n,void **data)
{
	Remove(n);
	if(data)
		*data = NodeData(n);
	SetData(n,NULL);
	FreeNode(n);

	name_count--;

	return STK_SUCCESS;
}

Node *stk_find_name_in_store(stk_name_store_t *store,char *name,Node *last)
{
	Node *start;
	unsigned char bucket = stk_determine_bucket(name);
	STK_DEBUG(STKA_NS,"searching for name %s in bucket %d store %p",name,bucket,store);

	if(!store) store = default_store;

	start = last ? NxtNode(last) : FirstNode(store->name_list[bucket]);
	for(Node *n = start; !AtListEnd(n); n = NxtNode(n)) {
		if(strcmp(((stk_name_request_data_t *)NodeData(n))->name,name) == 0)
			return n;
	}
	return NULL;
}

Node *stk_find_expired_names_in_store(stk_name_store_t *store,struct timeval *tv,Node *nxt)
{
	Node *start;

	if(!store) store = default_store;

	start = nxt ? nxt : stk_first_name_in_store(store);
	STK_DEBUG(STKA_NS,"find_expired_names_in_store %p %p %p",store,nxt,start);
	for(Node *n = start; n; n = stk_next_name_in_store(store,n)) {
		stk_name_request_data_t *request = (stk_name_request_data_t *)NodeData(n);

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

Node *stk_find_names_from_df_in_store(stk_name_store_t *store,stk_data_flow_t *df,Node *nxt)
{
	Node *start;

	if(!store) store = default_store;

	start = nxt ? nxt : stk_first_name_in_store(store);
	STK_DEBUG(STKA_NS,"find_names_from_df_in_store %p %p %p %p",store,nxt,start,df);
	for(Node *n = start; n; n = stk_next_name_in_store(store,n)) {
		stk_name_request_data_t *request = (stk_name_request_data_t *)NodeData(n);

		STK_DEBUG(STKA_NS,"find_names_from_df_in_store %p %p",request->df,df);
		if(request->df == df) {
			STK_DEBUG(STKA_NS,"store %p lingering %p %s",store,n,request->name);
			return n;
		}
	}
	return NULL;
}

stk_name_store_t *stk_find_name_store(char *group_name)
{
	if(!group_name || group_name[0] == '\0') return default_store;

	STK_DEBUG(STKA_NS,"searching for store group name %s",group_name);

	for(Node *n = FirstNode(store_groups); !AtListEnd(n); n = NxtNode(n)) {
		if(strcmp(((stk_name_store_t *)NodeData(n))->group_name,group_name) == 0)
			return NodeData(n);
	}

	return default_store;
}

stk_ret stk_iterate_name_stores(stk_name_store_cb cb,void *clientd)
{
	stk_ret rc;

	STK_DEBUG(STKA_NS,"iterating store groups %p",cb);

	for(Node *n = FirstNode(store_groups); !AtListEnd(n); n = NxtNode(n)) {
		rc = cb((stk_name_store_t *)NodeData(n),clientd);
		if(rc != STK_SUCCESS) return rc;
	}
	rc = cb((stk_name_store_t *)default_store,clientd);
	if(rc != STK_SUCCESS) return rc;

	return STK_SUCCESS;
}

Node *stk_first_name_in_store(stk_name_store_t *store)
{
	int bucket = 0;

	if(!store) store = default_store;

	while(bucket < 27 && IsPListEmpty(store->name_list[bucket])) bucket++;
	return bucket > 26 ? NULL : FirstNode(store->name_list[bucket]);
}

Node *stk_next_name_in_store(stk_name_store_t *store,Node *n)
{
	Node *nxt = NxtNode(n);

	if(!store) store = default_store;

	if(AtListEnd(nxt)) {
		/* Figure out which bucket this is in and return the first node of the next bucket that isn't empty */
		List *l = ListFromLastNode(n);
		int bucket = 0;

		while(bucket < 27 && l != store->name_list[bucket]) bucket++;
		bucket++;
		while(bucket < 27 && IsPListEmpty(store->name_list[bucket])) bucket++;
		nxt = bucket > 26 ? NULL : FirstNode(store->name_list[bucket]);
	}
	return nxt;
}

stk_name_store_t *stk_default_name_store() { return default_store; }

