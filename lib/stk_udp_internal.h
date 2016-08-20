/* 
 * Internal header for sharing structures between UDP listener and client data flow modules
 */
#ifndef STK_UDP_INTERNAL_H
#define STK_UDP_INTERNAL_H

/* In order of the way they appear on the wire... */
typedef struct stk_udp_wire_fragment_hdr_stct {
	stk_sequence_id seq_id;
	stk_uint64 num_fragments;
	stk_uint64 fragment_idx;			/* Should this be fragment offset? */
	stk_uint32 flags;
	stk_generation_id seq_generation;
	stk_uint32 unique_id;
} stk_udp_wire_fragment_hdr_t;

typedef struct stk_udp_wire_fragment0_hdr_stct {
	stk_uint64 total_len;			/* Length of all data sent */
	/* Sequence specific data */
	stk_sequence_type seq_type;
} stk_udp_wire_fragment0_hdr_t;

typedef struct stk_udp_wire_seqment_hdr_stct {
	stk_uint32 idx; /* index of fragment for segment starting at 0 */
	stk_uint32 len; /* Total len, not just whats contained in fragment */
	stk_uint32 offset;
	stk_uint64 type;
} stk_udp_wire_seqment_hdr_t;

#endif
