/** @file stk_sequence.h
 * This header provides the typedefs and definitions required to interface
 * to the Sequence module APIs
 */
#ifndef STK_SEQUENCE_H
#define STK_SEQUENCE_H

#include "stk_common.h"

/**
 * \typedef stk_sequence_id
 * A Sequence ID is a 64 bit unsigned number.
 * \see stk_acquire_sequence_id() and stk_create_sequence()
 */
typedef stk_uint64 stk_sequence_id;

/**
 * \typedef stk_sequence_type
 * A Sequence Type defines the type of data contained in the sequence.
 * This is largely left for user usage, though STK defines some typical types.
 * \see stk_create_sequence()
 */
typedef stk_uint16 stk_sequence_type;
/**
 * \typedef stk_generation_id
 * The generation ID is a 16 bit unsigned number and is used to manage uniqueness of the sequence.
 * For example, data flow modules will bump the generation each time a sequence is sent ensuring
 * each sequeunce sent is uniquely identifed.
 * \see stk_create_sequence()
 */
typedef stk_uint16 stk_generation_id;
/**
 * \typedef stk_sequence_metadata_t
 * Sequences can store data and meta data. This is the type used to manage meta data.
 * [Meta data is not yet implemented]
 */
typedef void * stk_sequence_metadata_t;

/**
 * \typedef stk_sequence_t
 * This is the Sequence object
 * \see stk_create_sequence()
 */
typedef struct stk_sequence_stct stk_sequence_t;

/**
 * \typedef stk_sequence_iterator_t
 * This defines an iterator to step through each data element in a sequence
 * \see stk_sequence_iterator() and stk_iterate_sequence()
 */
typedef struct stk_sequence_iterator_stct stk_sequence_iterator_t;

/** Definition of an invalid Sequence ID */
#define STK_SEQUENCE_ID_INVALID 0


#define STK_SEQUENCE_TYPE_INVALID 0   /*!< Invalid Sequence - never used */
#define STK_SEQUENCE_TYPE_DATA 1      /*!< Sequence is a Data sequence */
#define STK_SEQUENCE_TYPE_KVPAIR 2    /*!< Sequence is set of Key Value Pairs */
#define STK_SEQUENCE_TYPE_MGMT 3      /*!< Sequence is Management data */
#define STK_SEQUENCE_TYPE_REQUEST 4   /*!< Sequence is a request */
#define STK_SEQUENCE_TYPE_QUERY 5     /*!< Sequence is a query (read only request) */
#define STK_SEQUENCE_TYPE_SUBSCRIBE 6 /*!< Sequence is a subscription (persistent request) */

/** Macro to ease mapping of types to strings */
#define STK_SEQ_TYPE_TO_STRING(_type) \
	_type == STK_SEQUENCE_TYPE_DATA ? "Data" : \
	_type == STK_SEQUENCE_TYPE_MGMT ? "Management" : \
	_type == STK_SEQUENCE_TYPE_KVPAIR ? "Key/Value Pair" : \
	_type == STK_SEQUENCE_TYPE_REQUEST ? "Request" : \
	_type == STK_SEQUENCE_TYPE_QUERY ? "Query" : \
	""


/** The callback signature to be used for functions being passed to stk_iterate_sequence() */
typedef stk_ret (*stk_sequence_cb)(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd);

#endif
