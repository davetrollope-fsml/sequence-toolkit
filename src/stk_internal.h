#ifndef STK_INTERNAL_H
#define STK_INTERNAL_H

#include "stk_common.h"
#include "stk_assert_log.h"
#include "stk_df_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <pthread.h>

#define str_p(x) #x
#define int_to_string_p(x) str_p(x)

/* Structure types are used at the start of every structure to identify the data type
 * for both overloading of data in to APIs and for debugging. Thus there should never
 * be a structure type 0
 */
#define STK_STCT_ENV 0x100

#define STK_STCT_SEQUENCE 0x200
#define STK_STCT_SEQUENCE_DATA_REF 0x201
#define STK_STCT_SEQUENCE_DATA_COPY 0x202
#define STK_STCT_SEQUENCE_ITERATOR 0x203
#define STK_STCT_SEQUENCE_MERGED_SEQ 0x204

#define STK_STCT_SERVICE  0x300
#define STK_STCT_SERVICE_GROUP  0x310
#define STK_STCT_SERVICE_IN_GROUP 0x311

#define STK_STCT_TIMER_SET  0x400

#define STK_STCT_DATA_FLOW 0x500

#define STK_STCT_SMARTBEAT 0x600
#define STK_STCT_SMARTBEAT_SVC 0x601

#define STK_STCT_NAME_SERVICE 0x700
#define STK_STCT_NAME_SERVICE_ACTIVITY 0x701

typedef stk_uint16 stk_stct_type;

/* Allocation macros */
#define STK_ALLOC_BUF(_size) malloc(_size)

#define STK_CALLOC(_size) calloc(1,_size)

#define STK_REALLOC(_ptr,_size) (_ptr) = realloc(_ptr,_size)

#define STK_FREE(_ptr) free(_ptr)

#define STK_CALLOC_STCT(_stct_type,_type,_uptr) \
	STK_CALLOC_STCT_EX(_stct_type,_type,0,_uptr)

#define STK_CALLOC_STCT_EX(_stct_type,_type,_exsz,_uptr) do { \
	_type *_ptr; \
	_ptr = calloc(1,sizeof(_type) + _exsz); \
	if(_ptr) \
		_ptr->stct_type = _stct_type; \
	else \
		STK_ASSERT(STKA_MEM,_ptr!=NULL,"Failed to alloc structure type %d (%ld bytes)",_stct_type,sizeof(_type)); \
	_uptr = _ptr; /* Generates a compiler warning if there is a type mismatch */ \
} while(0)

#define STK_FREE_STCT(_stct_type,_ptr) do { \
	STK_ASSERT(STKA_MEM,_ptr->stct_type==_stct_type,"Mismatched structure types on free of 0x%p (%d != %d)",_ptr,_ptr->stct_type,_stct_type); \
	_ptr->stct_type=0; /* Clear the structure type so misuse after a free is easily identified */ \
	free(_ptr); \
} while(0)

#endif
