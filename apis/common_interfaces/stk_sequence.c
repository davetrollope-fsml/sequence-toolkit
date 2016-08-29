/* Copied from stk_sequence.c */
extern "C" {
	#include "../lib/PLists.h"
	#include "../lib/stk_internal.h"
	Node *stk_sequence_first_elem(stk_sequence_t *seq);
	Node *stk_sequence_next_elem(stk_sequence_t *seq,Node *n);
	void stk_sequence_node_data(Node *n,char **dptr,stk_uint64 *sz);
	stk_uint64 stk_sequence_node_type(Node *n);
	int stk_is_at_list_end(Node *n);
	stk_ret stk_copy_string_to_sequence(stk_sequence_t *seq,char *data_ptr,int sz, stk_uint64 user_type) { return stk_copy_to_sequence(seq,(void*)data_ptr,sz,user_type); }
	stk_ret stk_copy_chars_to_sequence(stk_sequence_t *seq,unsigned long data_ptr,stk_uint64 sz, stk_uint64 user_type) { return stk_copy_to_sequence(seq,(void*)data_ptr,sz,user_type); }
	unsigned long new_voidDataArray(int sz) { return (unsigned long) malloc(sz); }
	void delete_voidDataArray(unsigned long a) { return free((void *) a); }
	void voidDataArray_setitem(unsigned long data_ptr,int idx,char c)
	{ char *ptr = (char *)data_ptr; ptr[idx] = c; }
	stk_sequence_t *stk_ulong_seq_to_seq_ptr(unsigned long ptr) { return (stk_sequence_t *) ptr; }
	unsigned long stk_seq_ptr_to_ulong_seq(stk_sequence_t *ptr) { return (unsigned long) ptr; }
}
