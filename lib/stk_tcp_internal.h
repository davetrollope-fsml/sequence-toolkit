/* 
 * Internal header for sharing structures between server and client TCP data flow modules
 */
#ifndef STK_TCP_INTERNAL_H
#define STK_TCP_INTERNAL_H
#include "stk_common.h"

/*
 * The structure of data on the wire
 */
typedef struct stk_tcp_wire_basic_hdr_stct {
	stk_uint16 wire_version; /*!< Defines the wire protocol version being used */
	/**
	 * Defines the minimum wire compatibility 
	 * wire_version may be higher than wire_compat, but only when fields are added!
	 * Forcing an upgrade is acheived by raising wire_compat
	 */
	stk_uint16 wire_compat;
	stk_uint32 flags;        /*!< Varies by segment, see below */
	stk_sequence_id id;      /*!< ID of the sequence being sent */
	stk_sequence_type type;  /*!< Type of the sequence being sent */
	stk_uint16 pad;          /*!< avoid valgrind errors about uninitialized memory */
	stk_uint32 pad2;         /*!< avoid valgrind errors about uninitialized memory */
} stk_tcp_wire_basic_hdr_t;

#define STK_TCP_FLAG_SEGMENTS_FOLLOW 0x1          /*!< Flag indicating more sequence segments follow */
#define STK_TCP_FLAG_NAME_FOLLOWS 0x2             /*!< Flag indicating the sequence name follows the basic header */
#define STK_TCP_FLAG_EXTENSION 0x80000000         /*!< Flag indicating extended headers follow (for the future) */

/*
 * Macro to initialize stk_tcp_wire_basic_hdr_t
 */
#define STK_TCP_INIT_BASIC_HDR(_hdr,_seq,_flags) do { \
		(_hdr)->id = stk_get_sequence_id(_seq); \
		(_hdr)->type = stk_get_sequence_type(_seq); \
		(_hdr)->flags = (_flags);	\
		(_hdr)->wire_version = 1;	\
		(_hdr)->wire_compat = 1;	\
		(_hdr)->pad = 0;	\
		(_hdr)->pad2 = 0;	\
	} while(0)

/*
 * Wire format of segment header
 */
typedef struct stk_tcp_wire_segment_hdr_stct {
	stk_uint16 segment_id;
	stk_uint8 nblks;
	stk_uint8 blk_num;
	stk_uint32 segment_len;
	stk_uint64 user_type;
} stk_tcp_wire_seqment_hdr_t;

/*
 * Structure used to collect and manage data
 * while deserializing data coming off the wire
 */
typedef struct stk_tcp_wire_read_buf_stct {
	stk_uint64 orig_elem_start;
	stk_uint64 elem_start;
	stk_uint64 read;
	stk_uint64 sz;
	char *buf;
	stk_ret cb_rc;
	stk_data_flow_t *df;
	stk_sequence_iterator_t *seqiter;
	stk_uint8 blks_rcvd;
	stk_uint8 segment_idx;
	stk_uint16 last_sgmt_id;
	stk_tcp_wire_seqment_hdr_t segment_hdr;
} stk_tcp_wire_read_buf_t;
#define STK_TCP_PARSE_START(_rb) (_rb)->orig_elem_start = (_rb)->elem_start;
#define STK_TCP_PARSE_RESET(_rb) (_rb)->elem_start = (_rb)->orig_elem_start;

size_t stk_tcp_data_buffered(stk_tcp_wire_read_buf_t *readbuf);
#endif
