#include "stk_sequence_api.h"
#include "stk_sequence.h"
#include "stk_internal.h"
#include "stk_common.h"
#include "stk_sync_api.h"
#include "stk_options_api.h"
#include "PLists.h"
#include <string.h>
#include <sys/time.h>

#define STK_SEQUENCE_FLAG_ALLOCID 1

struct stk_sequence_stct
{
	stk_stct_type stct_type;
	stk_env_t *env;
	stk_sequence_id id;
	char *name;
	stk_sequence_type type;
	stk_generation_id generation;
	List *data_list;
	int flags;
	int refcnt;
	List *meta_data_list; /* untransmitted local meta data */
};

typedef struct stk_sequence_data_def_stct
{
	stk_stct_type stct_type;
	stk_uint64 allocsz;
	stk_uint64 sz;
	stk_uint64 user_type;
	void *data_ptr;
} stk_sequence_data_def_t;

struct stk_sequence_iterator_stct
{
	stk_stct_type stct_type;
	stk_sequence_t *seq;
	Node *curr;
};

static int seed;
stk_sequence_id stk_acquire_sequence_id(stk_env_t *env,stk_service_type type)
{
	if(seed == 0) {
		struct timeval tv;

		gettimeofday(&tv,NULL);
		srand((unsigned int) (tv.tv_sec + tv.tv_usec)); 
		seed = 1;
	}
	return (stk_sequence_id) (rand()<<((sizeof(int)*8)-1)|rand());
}

stk_ret stk_release_sequence_id(stk_env_t *env,stk_sequence_id id)
{
	return STK_SUCCESS;
}

/*
 *TODO: At some point, we should have a shared memory option so that sequences and all its reference data in
 * shared memory. Would need to alloc a semaphore for it too. No copy data would be allowed for
 * a shared sequence - but this would allow services to share data through a sequence without
 * having to call sequence APIs.
 * Shared memory should store multiple sequences - so it might be a shared memory manager etc..
 */
stk_sequence_t *stk_create_sequence(stk_env_t *env,char *name, stk_sequence_id id, stk_sequence_type type,stk_service_type svctype, stk_options_t *options)
{
	stk_sequence_t * seq;
	STK_CALLOC_STCT(STK_STCT_SEQUENCE,stk_sequence_t,seq);
	if(seq) {
		stk_generation_id gen_id = 0;

		if(options) { /* find option protects against this, but we can avoid the function call - performance tweak */
			gen_id = (stk_generation_id) stk_find_option(options,"generation",NULL); /* Obfuscated feature */
		}

		if(name) seq->name = strdup(name);
		seq->env = env;
		if(id == STK_SEQUENCE_ID_INVALID) {
			seq->id = stk_acquire_sequence_id(env,svctype);
			seq->flags |= STK_SEQUENCE_FLAG_ALLOCID;
		} else
			seq->id = id;
		seq->generation = gen_id;
		seq->type = type;
		seq->refcnt = 1;
	}
	return seq;
}

void stk_free_sequence_data_list(List *data_list)
{
	stk_sequence_data_def_t *datadef;

	if(data_list) {
		while(!IsPListEmpty(data_list)) {
			Node *n = FirstNode(data_list);
			Remove(n);
			datadef = (stk_sequence_data_def_t *) NodeData(n);

			if(datadef->stct_type == STK_STCT_SEQUENCE_DATA_COPY)
				if(datadef->data_ptr) free(datadef->data_ptr);

			FreeNode(n);
		}
		FreeList(data_list);
	}
}

stk_ret stk_destroy_sequence(stk_sequence_t *seq)
{
	STK_ASSERT(STKA_SEQ,seq->stct_type==STK_STCT_SEQUENCE,"destroy a sequence, the pointer was to a structure of type %d",seq->stct_type);

	if(STK_ATOMIC_DECR(&seq->refcnt) == 1) {
		if(seq->flags & STK_SEQUENCE_FLAG_ALLOCID)
			stk_release_sequence_id(seq->env,seq->id);

		stk_free_sequence_data_list(seq->data_list);
		stk_free_sequence_data_list(seq->meta_data_list);

		{ char *name = stk_get_sequence_name(seq); if(name) free(name); }

		STK_FREE_STCT(STK_STCT_SEQUENCE,seq);
	}
	return STK_SUCCESS;
}

void stk_hold_sequence(stk_sequence_t *seq)
{
	STK_ASSERT(STKA_SEQ,seq->stct_type==STK_STCT_SEQUENCE,"destroy a sequence, the pointer was to a structure of type %d",seq->stct_type);
	STK_ATOMIC_INCR(&seq->refcnt);
}

stk_ret stk_add_reference_to_sequence(stk_sequence_t *seq,void *data_ptr,stk_uint64 sz, stk_uint64 user_type)
{
	stk_sequence_data_def_t *datadef;
	Node *n;

	if(!seq) return !STK_SUCCESS;

	STK_CALLOC_STCT(STK_STCT_SEQUENCE_DATA_REF,stk_sequence_data_def_t,datadef);
	if(!datadef) return !STK_SUCCESS;

	datadef->allocsz = sz;
	datadef->sz = sz;
	datadef->user_type = user_type;
	datadef->data_ptr = data_ptr;
	STK_DEBUG(STKA_SEQ,"copy data_ptr stk_add_reference_to_sequence %p",datadef->data_ptr);

	n = NewNode();
	if(!n) {
		STK_FREE_STCT(STK_STCT_SEQUENCE_DATA_COPY,datadef);
		return !STK_SUCCESS;
	}

	SetData(n,datadef);

	if(!seq->data_list) seq->data_list = NewPList();
	STK_ASSERT(STKA_SEQ,seq->data_list!=NULL,"allocate sequence %p data list for reference data",seq);

	AddTail(seq->data_list,n);

	return STK_SUCCESS;
}

stk_ret stk_add_sequence_reference_in_sequence(stk_sequence_t *seq,stk_sequence_t *merge_seq, stk_uint64 user_type)
{
	stk_sequence_data_def_t *datadef;
	Node *n;

	if(!seq) return !STK_SUCCESS;

	STK_CALLOC_STCT(STK_STCT_SEQUENCE_MERGED_SEQ,stk_sequence_data_def_t,datadef);
	if(!datadef) return !STK_SUCCESS;

	datadef->allocsz = 0;
	datadef->sz = 0;
	datadef->user_type = user_type;
	datadef->data_ptr = merge_seq;
	STK_DEBUG(STKA_SEQ,"copy data_ptr stk_add_reference_to_sequence %p",datadef->data_ptr);

	n = NewNode();
	if(!n) {
		STK_FREE_STCT(STK_STCT_SEQUENCE_DATA_COPY,datadef);
		return !STK_SUCCESS;
	}

	SetData(n,datadef);

	if(!seq->data_list) seq->data_list = NewPList();
	STK_ASSERT(STKA_SEQ,seq->data_list!=NULL,"allocate sequence %p data list for reference data",seq);

	AddTail(seq->data_list,n);

	return STK_SUCCESS;
}

void *stk_realloc_data_in_sequence(stk_sequence_t *seq,stk_sequence_data_def_t *datadef,stk_uint64 sz)
{
	void *newdata;

	STK_ASSERT(STKA_SEQ,datadef->stct_type==STK_STCT_SEQUENCE_DATA_COPY,"Invalid data definition (%d) passed to stk_realloc_datadef_in_sequence",datadef->stct_type);
	newdata = datadef->data_ptr;
	STK_REALLOC(newdata,sz);
	STK_DEBUG(STKA_SEQ,"realloc data_ptr stk_realloc_data_in_sequence %p -> %p",datadef->data_ptr,newdata);
	if(newdata) {
		datadef->allocsz = sz;
		datadef->sz = sz;
		datadef->data_ptr = newdata;
	}
	return newdata;
}

Node *stk_ialloc_in_sequence(stk_sequence_t *seq,void *data_ptr,stk_uint64 sz, stk_uint64 user_type)
{
	stk_sequence_data_def_t *datadef;
	Node *n;

	if(!seq) return NULL;

	STK_CALLOC_STCT(STK_STCT_SEQUENCE_DATA_COPY,stk_sequence_data_def_t,datadef);
	if(!datadef) return NULL;

	datadef->allocsz = sz;
	datadef->sz = sz;
	datadef->user_type = user_type;
	datadef->data_ptr = malloc(sz);
	if(!datadef->data_ptr) {
		STK_FREE_STCT(STK_STCT_SEQUENCE_DATA_COPY,datadef);
		return NULL;
	}
	STK_DEBUG(STKA_SEQ,"malloc data_ptr stk_ialloc_in_sequence %p",datadef->data_ptr);
	if(data_ptr)
		memcpy(datadef->data_ptr,data_ptr,sz);

	n = NewNode();
	if(!n) {
		free(datadef->data_ptr);
		STK_FREE_STCT(STK_STCT_SEQUENCE_DATA_COPY,datadef);
		return NULL;
	}
	SetData(n,datadef);

	return n;
}

stk_ret stk_alloc_in_sequence(stk_sequence_t *seq,stk_uint64 sz, stk_uint64 user_type)
{
	Node *n = stk_ialloc_in_sequence(seq,NULL,sz,user_type);
	if(!n) return !STK_SUCCESS;

	if(!seq->data_list) seq->data_list = NewPList();
	STK_ASSERT(STKA_SEQ,seq->data_list!=NULL,"allocate sequence %p data list for copied data",seq);

	AddTail(seq->data_list,n);

	return STK_SUCCESS;
}


stk_ret stk_copy_to_sequence(stk_sequence_t *seq,void *data_ptr,stk_uint64 sz, stk_uint64 user_type)
{
	Node *n;

	STK_ASSERT(STKA_SEQ,data_ptr!=NULL,"Data is invalid");

	n = stk_ialloc_in_sequence(seq,data_ptr,sz,user_type);
	if(!n) return !STK_SUCCESS;

	if(!seq->data_list) seq->data_list = NewPList();
	STK_ASSERT(STKA_SEQ,seq->data_list!=NULL,"allocate sequence %p data list for copied data",seq);

	AddTail(seq->data_list,n);

	return STK_SUCCESS;
}

stk_ret stk_copy_to_sequence_meta_data(stk_sequence_t *seq,void *data_ptr,stk_uint64 sz, stk_uint64 user_type)
{
	Node *n;

	STK_ASSERT(STKA_SEQ,data_ptr!=NULL,"Data is invalid");
	n = stk_ialloc_in_sequence(seq,data_ptr,sz,user_type);
	if(!n) return !STK_SUCCESS;

	if(!seq->meta_data_list) seq->meta_data_list = NewPList();
	STK_ASSERT(STKA_SEQ,seq->meta_data_list!=NULL,"allocate sequence %p meta data list for copied data",seq);

	AddTail(seq->meta_data_list,n);

	return STK_SUCCESS;
}

stk_uint64 stk_remove_sequence_data_by_type(stk_sequence_t *seq, stk_uint64 user_type, stk_uint64 max_instances)
{
	stk_uint64 removed = 0;
	if(!seq) return !STK_SUCCESS;

	if(!IsPListEmpty(seq->data_list)) {
		stk_sequence_data_def_t *datadef;

		for(Node *n = FirstNode(seq->data_list); (removed < max_instances || max_instances == 0) && !AtListEnd(n);) {
			datadef = (stk_sequence_data_def_t *) NodeData(n);
			if(datadef->user_type == user_type) {
				Node *c = n;
				n = NxtNode(n);
				Remove(c);
				removed++;

				if(datadef->stct_type == STK_STCT_SEQUENCE_DATA_COPY) {
					void *p = datadef->data_ptr;
					datadef->data_ptr = (void *) 0xdeadbeef;
					free(p);
					STK_FREE_STCT(STK_STCT_SEQUENCE_DATA_COPY,datadef);
				}
				else
					STK_FREE_STCT(STK_STCT_SEQUENCE_DATA_REF,datadef);
				
				SetData(c,NULL);
				FreeNode(c);
			}
			else
				n = NxtNode(n);
		}
	}

	return removed;
}

stk_generation_id stk_bump_sequence_generation(stk_sequence_t *seq)
{
	return STK_ATOMIC_INCR(&seq->generation);
}

stk_generation_id stk_get_sequence_generation(stk_sequence_t *seq)
{
	return seq->generation;
}

stk_ret stk_update_ref_data_in_sequence(stk_sequence_t *seq, stk_generation_id *gen_id)
{
	/* Duplicate the buffer for any referenced data and bump the generation number */
	STK_LOG(STK_LOG_ERROR,"Data archiving not implemented yet");
	return !STK_SUCCESS;
}

stk_ret stk_iterate_complete_sequence(stk_sequence_t *seq,
	stk_sequence_cb before_cb, stk_sequence_cb element_cb, stk_sequence_cb after_cb, void *clientd)
{
	stk_sequence_iterator_t *seqiter;
	stk_ret rc = STK_SUCCESS;
	stk_uint64 seq_type;

	if(seq->stct_type==STK_STCT_SEQUENCE_ITERATOR) {
		seqiter = (stk_sequence_iterator_t *)seq;
		seq = seqiter->seq;
	} else
		seqiter = NULL;

	STK_ASSERT(STKA_SEQ,seq!=NULL,"sequence null or invalid :%p",seq);
	STK_ASSERT(STKA_SEQ,seq->stct_type==STK_STCT_SEQUENCE,"sequence %p passed in to stk_iterate_sequence is structure type %d",seq,seq->stct_type);
	if(seqiter) STK_ASSERT(STKA_SEQ,seq==seqiter->seq,"The iterator %p was initialized with sequence %p but sequence %p was passed to stk_iterate_sequence()",seqiter,seqiter->seq,seq);

	if(before_cb || after_cb)
		seq_type = (stk_uint64) stk_get_sequence_type(seq);
	else
		seq_type = (stk_uint64) NULL;

	if(before_cb) {
		rc = before_cb(seq,NULL,0,seq_type,clientd);
		if(rc != STK_SUCCESS) {
			if(after_cb) after_cb(seq,NULL,0,seq_type,clientd);
			return rc;
		}
	}

	if(!seq->data_list) {
		/* No elements to iterate over */
		if(after_cb) 
			return after_cb(seq,NULL,0,seq_type,clientd);
		else
			return STK_SUCCESS;
	}

	if(!IsPListEmpty(seq->data_list)) {
		for(Node *n = FirstNode(seq->data_list); !AtListEnd(n); n = NxtNode(n)) {
			stk_sequence_data_def_t *datadef = (stk_sequence_data_def_t *) NodeData(n);
			if(datadef->stct_type != STK_STCT_SEQUENCE_MERGED_SEQ) {
				if(seqiter) seqiter->curr = n;
				rc = element_cb(seq,datadef->data_ptr,datadef->sz,datadef->user_type,clientd);
				if(rc != STK_SUCCESS) {
					if(after_cb) after_cb(seq,NULL,0,seq_type,clientd);
					return rc;
				}
			} else {
				if(seqiter) {
					/* TODO: This might work but is not tested - not sure if calling next() after this works */
					seqiter->seq = datadef->data_ptr;
					rc = stk_iterate_sequence((stk_sequence_t *) seqiter,element_cb,clientd);
					seqiter->seq = seq;
					if(rc != STK_SUCCESS) {
						if(after_cb) after_cb(seq,NULL,0,seq_type,clientd);
						return rc;
					}
				} else {
					rc = stk_iterate_sequence((stk_sequence_t *) datadef->data_ptr,element_cb,clientd);
					if(rc != STK_SUCCESS) {
						if(after_cb) after_cb(seq,NULL,0,seq_type,clientd);
						return rc;
					}
				}
			}
		}
	}
	if(after_cb) return after_cb(seq,NULL,0,seq_type,clientd);
	return STK_SUCCESS;
}

stk_ret stk_iterate_sequence(stk_sequence_t *seq,stk_sequence_cb element_cb,void *clientd)
{
	return stk_iterate_complete_sequence(seq,NULL,element_cb,NULL,clientd);
}


stk_ret stk_sequence_find_data_list_by_type(
	stk_sequence_t *seq,stk_sequence_iterator_t *seqiter,List *data_list,
	stk_uint64 user_type,void **data_ptr,stk_uint64 *sz)
{
	if(!data_list) return STK_NOT_FOUND; /* No elements to iterate over */

	if(!IsPListEmpty(data_list)) {
		for(Node *n = FirstNode(data_list); !AtListEnd(n); n = NxtNode(n)) {
			stk_sequence_data_def_t *datadef = (stk_sequence_data_def_t *) NodeData(n);
			if(seqiter) seqiter->curr = n;
			if(datadef->user_type == user_type) {
				*data_ptr = datadef->data_ptr;
				*sz = datadef->sz;
				return STK_SUCCESS;
			}
		}
	}
	return STK_NOT_FOUND;
}

stk_ret stk_sequence_find_meta_data_by_type(stk_sequence_t *seq,stk_uint64 user_type,void **data_ptr,stk_uint64 *sz)
{
	/* The sequence iterator logic is copied here from find_data_by_type, but I'm not sure if it makes sense.
	 * so only supporting sequences passed in right now, but perhaps in the future this makes sense.
	 */
	stk_sequence_iterator_t *seqiter;
	stk_ret rc = STK_SUCCESS;

	*data_ptr = NULL;
	*sz = 0;

	if(seq->stct_type==STK_STCT_SEQUENCE_ITERATOR) {
		seqiter = (stk_sequence_iterator_t *)seq;
		seq = seqiter->seq;
	} else
		seqiter = NULL;

	STK_ASSERT(STKA_SEQ,seq!=NULL,"sequence null or invalid :%p",seq);
	STK_ASSERT(STKA_SEQ,seq->stct_type==STK_STCT_SEQUENCE,"sequence %p passed in to stk_iterate_sequence is structure type %d",seq,seq->stct_type);
	if(seqiter) STK_ASSERT(STKA_SEQ,seq==seqiter->seq,"The iterator %p was initialized with sequence %p but sequence %p was passed to stk_iterate_sequence()",seqiter,seqiter->seq,seq);

	return stk_sequence_find_data_list_by_type(seq,seqiter,seq->meta_data_list,user_type,data_ptr,sz);
}

stk_ret stk_sequence_find_data_by_type(stk_sequence_t *seq,stk_uint64 user_type,void **data_ptr,stk_uint64 *sz)
{
	stk_sequence_iterator_t *seqiter;
	stk_ret rc = STK_SUCCESS;

	*data_ptr = NULL;
	*sz = 0;

	if(seq->stct_type==STK_STCT_SEQUENCE_ITERATOR) {
		seqiter = (stk_sequence_iterator_t *)seq;
		seq = seqiter->seq;
	} else
		seqiter = NULL;

	STK_ASSERT(STKA_SEQ,seq!=NULL,"sequence null or invalid :%p",seq);
	STK_ASSERT(STKA_SEQ,seq->stct_type==STK_STCT_SEQUENCE,"sequence %p passed in to stk_iterate_sequence is structure type %d",seq,seq->stct_type);
	if(seqiter) STK_ASSERT(STKA_SEQ,seq==seqiter->seq,"The iterator %p was initialized with sequence %p but sequence %p was passed to stk_iterate_sequence()",seqiter,seqiter->seq,seq);

	return stk_sequence_find_data_list_by_type(seq,seqiter,seq->data_list,user_type,data_ptr,sz);
}

stk_sequence_id stk_get_sequence_id(stk_sequence_t *seq) { return seq->id; }

stk_ret stk_set_sequence_id(stk_sequence_t *seq, stk_sequence_id id) { seq->id = id; return STK_SUCCESS; }

stk_sequence_type stk_get_sequence_type(stk_sequence_t *seq) { return seq->type; }

stk_ret stk_set_sequence_type(stk_sequence_t *seq,stk_sequence_type type ) { seq->type = type; return STK_SUCCESS; }

char *stk_get_sequence_name(stk_sequence_t *seq) { return seq->name; }

stk_ret stk_set_sequence_name(stk_sequence_t *seq, char *name)
{
	if(seq->name) free(seq->name);
	seq->name = name;
	return STK_SUCCESS;
}

int stk_number_of_sequence_elements(stk_sequence_t *seq)
{
	STK_ASSERT(STKA_SEQ,seq!=NULL,"sequence null or invalid :%p",seq);
	STK_ASSERT(STKA_SEQ,seq->stct_type==STK_STCT_SEQUENCE,"sequence %p passed in to stk_number_of_sequence_elements is structure type %d",seq,seq->stct_type);
	if(!seq->data_list)
		return 0;
	else {
		/* return NodeCount(seq->data_list); - could do this if we knew sequences had merged sequences - chance for a flag perhaps but NodeCount() isn't optimized either so... */
		int count = 0;

		for(Node *n = FirstNode(seq->data_list); !AtListEnd(n); n = NxtNode(n)) {
			stk_sequence_data_def_t *datadef = (stk_sequence_data_def_t *) NodeData(n);
			if(datadef->stct_type != STK_STCT_SEQUENCE_MERGED_SEQ)
				count++;
			else
				count += stk_number_of_sequence_elements(datadef->data_ptr);
		}

		return count;
	}
}

int stk_has_any_sequence_elements(stk_sequence_t *seq)
{
	STK_ASSERT(STKA_SEQ,seq!=NULL,"sequence null or invalid :%p",seq);
	STK_ASSERT(STKA_SEQ,seq->stct_type==STK_STCT_SEQUENCE,"sequence %p passed in to stk_number_of_sequence_elements is structure type %d",seq,seq->stct_type);
	if(!seq->data_list || IsPListEmpty(seq->data_list))
		return 0;
	else
		return 1;
}

void *stk_last_sequence_element(stk_sequence_t *seq)
{
	STK_ASSERT(STKA_SEQ,seq!=NULL,"sequence null or invalid :%p",seq);
	STK_ASSERT(STKA_SEQ,seq->stct_type==STK_STCT_SEQUENCE,"sequence %p passed in to stk_last_sequence_element is structure type %d",seq,seq->stct_type);
	STK_ASSERT(STKA_SEQ,seq->data_list!=NULL,"sequence %p passed in to stk_last_sequence_element is not initialized with any segments",seq);
	STK_ASSERT(STKA_SEQ,!IsPListEmpty(seq->data_list),"sequence %p passed in to stk_last_sequence_element is empty",seq);

	return ((stk_sequence_data_def_t *)NodeData(LastNode(seq->data_list)))->data_ptr;
}

stk_env_t *stk_env_from_sequence(stk_sequence_t *seq)
{
	STK_ASSERT(STKA_SEQ,seq!=NULL,"sequence null or invalid :%p",seq);
	STK_ASSERT(STKA_SEQ,seq->stct_type==STK_STCT_SEQUENCE,"sequence %p passed in to stk_env_from_sequence is structure type %d",seq,seq->stct_type);
	return seq->env;
}

stk_sequence_iterator_t *stk_sequence_iterator(stk_sequence_t *seq)
{
	stk_sequence_iterator_t *seqiter = NULL;

	STK_ASSERT(STKA_SEQ,seq!=NULL,"sequence null or invalid :%p",seq);
	STK_ASSERT(STKA_SEQ,seq->stct_type==STK_STCT_SEQUENCE,"sequence %p passed in to stk_sequence_iterator is structure type %d",seq,seq->stct_type);

	STK_CALLOC_STCT(STK_STCT_SEQUENCE_ITERATOR,stk_sequence_iterator_t,seqiter);
	if(seqiter) {
		seqiter->seq = seq;
		if(seq->data_list && !IsPListEmpty(seq->data_list))
			seqiter->curr = FirstNode(seqiter->seq->data_list);
	}
	return seqiter;
}

stk_ret stk_end_sequence_iterator(stk_sequence_iterator_t *seqiter)
{
	STK_FREE_STCT(STK_STCT_SEQUENCE_ITERATOR,seqiter);
	return STK_SUCCESS;
}

void *stk_sequence_iterator_data(stk_sequence_iterator_t *seqiter)
{
	STK_ASSERT(STKA_SEQ,seqiter!=NULL,"sequence iterator null or invalid :%p",seqiter);
	STK_ASSERT(STKA_SEQ,seqiter->stct_type==STK_STCT_SEQUENCE_ITERATOR,"sequence iterator %p passed in to stk_sequence_iterator_data is structure type %d",seqiter,seqiter->stct_type);

	if(!seqiter->curr) return NULL;

	{
	stk_sequence_data_def_t *datadef = (stk_sequence_data_def_t *) NodeData(seqiter->curr);
	return datadef->data_ptr;
	}
}

void *stk_sequence_iterator_next(stk_sequence_iterator_t *seqiter)
{
	STK_ASSERT(STKA_SEQ,seqiter!=NULL,"sequence iterator null or invalid :%p",seqiter);
	STK_ASSERT(STKA_SEQ,seqiter->stct_type==STK_STCT_SEQUENCE_ITERATOR,"sequence iterator %p passed in to stk_sequence_iterator_next is structure type %d",seqiter,seqiter->stct_type);
	if(seqiter->curr) {
		if (!IsLastNode(seqiter->curr)) {
			seqiter->curr = NxtNode(seqiter->curr);
			stk_sequence_data_def_t *datadef = (stk_sequence_data_def_t *) NodeData(seqiter->curr);
			return datadef->data_ptr;
		}
		else {
			seqiter->curr = NULL;
			return NULL;
		}
	} else {
		if(seqiter->seq->data_list && !IsPListEmpty(seqiter->seq->data_list)) {
			seqiter->curr = FirstNode(seqiter->seq->data_list);
			STK_ASSERT(STKA_SEQ,seqiter->curr != NULL,"Null first node in non empty list in stk_sequence_iterator_next");

			{
				stk_sequence_data_def_t *datadef = (stk_sequence_data_def_t *) NodeData(seqiter->curr);
				return datadef->data_ptr;
			}
		}
		else
			return NULL;
	}
}

void *stk_sequence_iterator_prev(stk_sequence_iterator_t *seqiter)
{
	STK_ASSERT(STKA_SEQ,seqiter!=NULL,"sequence iterator null or invalid :%p",seqiter);
	STK_ASSERT(STKA_SEQ,seqiter->stct_type==STK_STCT_SEQUENCE_ITERATOR,"sequence iterator %p passed in to stk_sequence_iterator_prev is structure type %d",seqiter,seqiter->stct_type);
	if(seqiter->curr) {
		if(FirstNode(seqiter->seq->data_list) == seqiter->curr) {
			seqiter->curr = NULL;
			return seqiter->curr;
		}

		seqiter->curr = PrvNode(seqiter->curr);
		if (seqiter->curr) {
			stk_sequence_data_def_t *datadef = (stk_sequence_data_def_t *) NodeData(seqiter->curr);
			return datadef->data_ptr;
		}
		else
			return NULL;
	} else {
		if(seqiter->seq->data_list && !IsPListEmpty(seqiter->seq->data_list)) {
			seqiter->curr = LastNode(seqiter->seq->data_list);
			if (seqiter->curr) {
				stk_sequence_data_def_t *datadef = (stk_sequence_data_def_t *) NodeData(seqiter->curr);
				return datadef->data_ptr;
			}
			else
				return NULL;
		}
		else
			return NULL;
	}
}

stk_ret stk_sequence_iterator_copy_data(stk_sequence_iterator_t *seqiter,void *data_ptr,stk_uint64 sz)
{
	STK_ASSERT(STKA_SEQ,seqiter!=NULL,"sequence iterator null or invalid :%p",seqiter);
	STK_ASSERT(STKA_SEQ,seqiter->stct_type==STK_STCT_SEQUENCE_ITERATOR,"sequence iterator %p passed in to stk_sequence_iterator_copy_data is structure type %d",seqiter,seqiter->stct_type);
	if(!seqiter->curr) return !STK_SUCCESS;

	{
	stk_sequence_data_def_t *datadef = NodeData(seqiter->curr);

	if(sz > datadef->allocsz)
		return !STK_SUCCESS; /* Allocated data too small */

	datadef->sz = sz;
	memcpy(datadef->data_ptr,data_ptr,sz);
	}
	return STK_SUCCESS;
}

stk_uint64 stk_sequence_iterator_data_size(stk_sequence_iterator_t *seqiter)
{
	STK_ASSERT(STKA_SEQ,seqiter!=NULL,"sequence iterator null or invalid :%p",seqiter);
	STK_ASSERT(STKA_SEQ,seqiter->stct_type==STK_STCT_SEQUENCE_ITERATOR,"sequence iterator %p passed in to stk_sequence_iterator_data_size is structure type %d",seqiter,seqiter->stct_type);
	if(!seqiter->curr) return 0;

	{
	stk_sequence_data_def_t *datadef = NodeData(seqiter->curr);
	return datadef->sz;
	}
}

stk_uint64 stk_sequence_iterator_alloc_size(stk_sequence_iterator_t *seqiter)
{
	STK_ASSERT(STKA_SEQ,seqiter!=NULL,"sequence iterator null or invalid :%p",seqiter);
	STK_ASSERT(STKA_SEQ,seqiter->stct_type==STK_STCT_SEQUENCE_ITERATOR,"sequence iterator %p passed in to stk_sequence_iterator_alloc_size is structure type %d",seqiter,seqiter->stct_type);
	if(!seqiter->curr) return 0;

	{
	stk_sequence_data_def_t *datadef = NodeData(seqiter->curr);
	return datadef->allocsz;
	}
}

stk_ret stk_sequence_iterator_set_size(stk_sequence_iterator_t *seqiter,stk_uint64 sz)
{
	STK_ASSERT(STKA_SEQ,seqiter!=NULL,"sequence iterator null or invalid :%p",seqiter);
	STK_ASSERT(STKA_SEQ,seqiter->stct_type==STK_STCT_SEQUENCE_ITERATOR,"sequence iterator %p passed in to stk_sequence_iterator_alloc_size is structure type %d",seqiter,seqiter->stct_type);
	if(!seqiter->curr) return !STK_SUCCESS;

	{
	stk_sequence_data_def_t *datadef = NodeData(seqiter->curr);
	if(sz > datadef->allocsz) return !STK_SUCCESS;
	datadef->sz = sz;
	}
	return STK_SUCCESS;
}

stk_ret stk_sequence_iterator_set_user_type(stk_sequence_iterator_t *seqiter,stk_uint64 user_type)
{
	STK_ASSERT(STKA_SEQ,seqiter!=NULL,"sequence iterator null or invalid :%p",seqiter);
	STK_ASSERT(STKA_SEQ,seqiter->stct_type==STK_STCT_SEQUENCE_ITERATOR,"sequence iterator %p passed in to stk_sequence_iterator_alloc_size is structure type %d",seqiter,seqiter->stct_type);
	if(!seqiter->curr) return !STK_SUCCESS;

	{
	stk_sequence_data_def_t *datadef = NodeData(seqiter->curr);
	datadef->user_type = user_type;
	}
	return STK_SUCCESS;
}

void *stk_sequence_iterator_realloc_segment(stk_sequence_iterator_t *seqiter,stk_uint64 sz)
{
	stk_sequence_data_def_t *datadef = NodeData(seqiter->curr);

	STK_ASSERT(STKA_SEQ,seqiter!=NULL,"sequence iterator null or invalid :%p",seqiter);
	STK_ASSERT(STKA_SEQ,seqiter->stct_type==STK_STCT_SEQUENCE_ITERATOR,"sequence iterator %p passed in to stk_sequence_iterator_alloc_size is structure type %d",seqiter,seqiter->stct_type);
	if(!seqiter->curr) return NULL;

	return stk_realloc_data_in_sequence(seqiter->seq,datadef,sz);
}

void *stk_sequence_iterator_ensure_segment_size(stk_sequence_iterator_t *seqiter,stk_uint64 sz)
{
	stk_uint64 asz = stk_sequence_iterator_alloc_size(seqiter);
	stk_ret rc;

	if(sz > asz) {
		void *data = stk_sequence_iterator_realloc_segment(seqiter,sz);
		rc = stk_sequence_iterator_set_size(seqiter,sz);
		STK_ASSERT(STKA_SEQ,rc==STK_SUCCESS,"re-set sequence size");
		return data;
	}

	{
	stk_sequence_data_def_t *datadef = NodeData(seqiter->curr);
	rc = stk_sequence_iterator_set_size(seqiter,sz);
	STK_ASSERT(STKA_SEQ,rc==STK_SUCCESS,"re-set sequence size");
	return datadef->data_ptr;
	}
}

/* Python APIs - not made public until a better iterator API is created */
Node *stk_sequence_first_elem(stk_sequence_t *seq) { return FirstNode(seq->data_list); }
Node *stk_sequence_next_elem(stk_sequence_t *seq,Node *n) { return NxtNode(n); }
stk_uint64 stk_sequence_node_type(Node *n) { return ((stk_sequence_data_def_t *)NodeData(n))->user_type; }
void stk_sequence_node_data(Node *n,char **dptr,stk_uint64 *sz)
{
	stk_sequence_data_def_t *def = NodeData(n);
	*dptr = def->data_ptr;
	*sz = def->sz;
}

int stk_is_at_list_end(Node *n) { return AtListEnd(n) ? 1 : 0; }
