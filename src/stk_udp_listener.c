#include "stk_data_flow_api.h"
#include "stk_data_flow.h"
#include "stk_common.h"
#include "stk_internal.h"
#include "stk_env.h"
#include "stk_sequence.h"
#include "stk_sequence_api.h"
#include "stk_rawudp_api.h"
#include "stk_udp_listener_api.h"
#include "stk_udp.h"
#include "stk_udp_internal.h"
#include "stk_ports.h"
#include "stk_options_api.h"
#include "stk_sync_api.h"
#include "stk_timer_api.h"

#include <sys/time.h>

#define STK_DUMP_RCV_HEX 1

#ifdef __APPLE_
#define _DARWIN_C_SOURCE
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/param.h> /* for MIN */
#include <fcntl.h>
#include <limits.h>

#define STK_MAX_IOV IOV_MAX
#ifdef __APPLE__
#define STK_NB_SEND_FLAGS 0
#else
#define STK_NB_SEND_FLAGS MSG_NOSIGNAL
#endif

#ifndef STK_UDP_WIRE_DBG
#define STK_UDP_WIRE_DBG(...) STK_DEBUG(STKA_NET, __VA_ARGS__)
#endif
#ifndef STK_UDP_DBG
#define STK_UDP_DBG(...) STK_DEBUG(STKA_NET, __VA_ARGS__)
#endif

/* TODO - make timer interval configurable */
#define DEFAULT_SEQ_EXPIRATION_IVL 200 /* ms */

stk_timer_set_t *stk_udp_listener_timers;
static int timer_refcount;

stk_ret stk_udp_listener_destroy_data_flow(stk_data_flow_t *flow);
stk_sequence_t *stk_udp_listener_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);
stk_ret stk_udp_listener_data_flow_buffered(stk_data_flow_t *flow);
char *stk_udp_listener_data_flow_protocol(stk_data_flow_t *flow);
stk_ret stk_udp_client_data_flow_send(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags);

static stk_data_flow_module_t udp_listener_fptrs = {
	stk_udp_listener_create_data_flow, stk_udp_listener_destroy_data_flow,
	stk_udp_client_data_flow_send, stk_udp_listener_data_flow_rcv,
	stk_udp_listener_data_flow_id_ip, stk_udp_listener_data_flow_buffered,
	stk_udp_listener_data_flow_protocol
};

typedef struct stk_udp_saved_segment_stct {
	stk_udp_wire_seqment_hdr_t seg_hdr;
	int data_len;
	char data[65536];
} stk_udp_saved_segment_t;

typedef struct stk_udp_partial_seq_stct {
	stk_sequence_t *sequence;
	stk_uint64 num_rcvd_fragments;
	stk_udp_saved_segment_t *segments;
	int num_segments;
	struct timeval create_time;
	stk_uint32 unique_id;
} stk_udp_partial_seq_t;

typedef struct stk_udp_assembler_stats_stct stk_udp_assembler_stats_t;
typedef struct stk_udp_assembler_opts_stct stk_udp_assembler_opts_t;

typedef struct stk_udp_assembler_stct {
	stk_udp_wire_read_buf_t *raw_bufread;
	struct stk_udp_assembler_stats_stct {
		stk_uint64 num_rcvd_fragments;
		stk_uint64 complete_sequences;
	} stats;
	stk_uint32 low_segment_offset;
	stk_udp_partial_seq_t *sequences;
	stk_udp_partial_seq_t *end_sequence; /* past mem end */
	stk_timer_t *seq_expiration_timer;
	struct stk_udp_assembler_opts_stct {
		struct timeval expiration_interval;
	} opts;
} stk_udp_assembler_t;

stk_ret stk_log_reassembler_stats(stk_udp_assembler_t *asmblr);

typedef struct stk_udp_listener_stct {
	stk_data_flow_t *rawudp_df;
	struct sockaddr_in server_addr;
	stk_udp_assembler_t asmblr;
} stk_udp_listener_t;

stk_ret stk_udp_save_segment(stk_udp_assembler_t *asmblr,stk_udp_partial_seq_t *pseq,stk_sequence_t *seq,stk_udp_wire_seqment_hdr_t *seg_hdr,int curr_offset);

extern void stk_dump_hex(unsigned char *ptr, ssize_t ret, int offset);

stk_data_flow_t *stk_udp_listener_create_data_flow(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options)
{
	stk_data_flow_t *df = stk_alloc_data_flow(env,STK_UDP_LISTENER_FLOW,name,id,sizeof(stk_udp_listener_t),&udp_listener_fptrs,options);
	stk_udp_listener_t *ts = stk_data_flow_module_data(df);

	/* substitute callbacks for internal callbacks in options?? */
	{ /* This should probably be in a reassembler function */
	void *expiration_ivl_str = stk_find_option(options,"sequence_expiration_interval",NULL);
	if (expiration_ivl_str) {
		int ivl = atoi(expiration_ivl_str);
		STK_UDP_DBG("sequence expiration interval %d",ivl);
		ts->asmblr.opts.expiration_interval.tv_sec = ivl/1000;
		ts->asmblr.opts.expiration_interval.tv_usec = (ivl%1000)*1000;
	} else {
		ts->asmblr.opts.expiration_interval.tv_sec = 1;
		ts->asmblr.opts.expiration_interval.tv_usec = 0;
	}
	}

	{
	stk_options_t *extended_options;
	extended_options = stk_copy_extend_options(options, 1);
	stk_ret rc;

	rc = stk_append_option(extended_options, "callback_data_flow", (void *) df);
	STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"append callback data flow to raw listener options");

	ts->rawudp_df = stk_rawudp_listener_create_data_flow(env,name,id,extended_options);
	if(!ts->rawudp_df) {
		stk_udp_listener_destroy_data_flow(df);
		return NULL;
	}

	rc = stk_free_options(extended_options);
	STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"free extended options for name service");
	}

	{
	stk_ret rc = stk_rawudp_listener_data_flow_clientip(ts->rawudp_df,(struct sockaddr *) &ts->server_addr,sizeof(ts->server_addr));
	STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"getting raw listeners client ip in udp listener create");
	}

	if(stk_udp_listener_timers == NULL) {
		stk_udp_listener_timers = stk_new_timer_set(env,NULL,0,STK_TRUE);
		STK_ASSERT(STKA_NET,stk_udp_listener_timers!=NULL,"allocate a timer set for UDP listeners");
	}
	STK_ATOMIC_INCR(&timer_refcount);
	return df;
}

stk_ret stk_udp_listener_destroy_data_flow(stk_data_flow_t *df)
{
	stk_udp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	stk_ret ret;

	if(ts->rawudp_df) {
		ret = stk_destroy_data_flow(ts->rawudp_df);
		STK_ASSERT(STKA_NET,ret==STK_SUCCESS,"unhook rawudp data flow %p",df);
	}

	ret = stk_free_data_flow(df);

	if(STK_ATOMIC_DECR(&timer_refcount) == 1) {
		stk_ret rc = stk_free_timer_set(stk_udp_listener_timers,STK_TRUE);
		stk_udp_listener_timers = NULL;
		STK_ASSERT(STKA_NET,rc == STK_SUCCESS,"free timer set for UDP listeners");
	}

	return ret;
}

int stk_udp_listener_fd(stk_data_flow_t *svr_df)
{
	stk_udp_listener_t *ts = stk_data_flow_module_data(svr_df); /* Asserts on structure type */
	return stk_rawudp_listener_fd(ts->rawudp_df);
}

stk_ret stk_udp_save_segment(stk_udp_assembler_t *asmblr,stk_udp_partial_seq_t *pseq,stk_sequence_t *seq,stk_udp_wire_seqment_hdr_t *seg_hdr,int curr_offset)
{
	stk_udp_wire_read_buf_t *rawudp_bufread = asmblr->raw_bufread;
	stk_udp_wire_fragment_hdr_t *hdr = (stk_udp_wire_fragment_hdr_t *) rawudp_bufread->buf;
	int segment_idx = pseq->num_segments - 1;

	asmblr->low_segment_offset = (pseq->segments == NULL || seg_hdr->offset < asmblr->low_segment_offset ? seg_hdr->offset : asmblr->low_segment_offset);

	/* Search for a segment with data_len 0 and use it, or realloc to add new space
	 * Probably a better way to track this, but optimise later!!
	 */
	while(pseq->segments && segment_idx >= 0 && pseq->segments[segment_idx].data_len) segment_idx--;

	if((!pseq->segments) || segment_idx < 0) {
		pseq->segments = realloc(pseq->segments,++pseq->num_segments * sizeof(pseq->segments[0]));
		STK_ASSERT(STKA_NET,pseq->segments!=NULL,"saving segment");
		segment_idx = pseq->num_segments - 1;
	}

	memcpy(&pseq->segments[segment_idx].seg_hdr,seg_hdr,sizeof(*seg_hdr));

	pseq->segments[segment_idx].data_len = 
		MIN(seg_hdr->len - seg_hdr->offset,rawudp_bufread->read - curr_offset);
	memcpy(&pseq->segments[segment_idx].data,&rawudp_bufread->buf[curr_offset],
		pseq->segments[segment_idx].data_len);

	return STK_SUCCESS;
}

stk_ret stk_reassembler_del_partial_sequence(stk_udp_assembler_t *asmblr, stk_udp_partial_seq_t *curr_seq)
{
	STK_UDP_DBG("reassembler del seq %p n %p end %p curr %p",curr_seq->sequence,asmblr->sequences,asmblr->end_sequence,curr_seq);
	if(curr_seq == asmblr->end_sequence - 1) {
		if(curr_seq == asmblr->sequences) {
			/* Removing last entry */
			free(asmblr->sequences);
			asmblr->sequences = NULL;
			asmblr->end_sequence = NULL;
			return STK_SUCCESS;
		}

		/* Optimization to avoid calling memcpy */
		--asmblr->end_sequence;
		return STK_SUCCESS;
	}

	/* Shift the table */
	memcpy(curr_seq,(curr_seq+1),(asmblr->end_sequence-curr_seq)/sizeof(*curr_seq));
	asmblr->end_sequence--;
	return STK_SUCCESS;
}

stk_ret stk_reassembler_del_sequence(stk_udp_assembler_t *asmblr, stk_sequence_t *seq)
{
	stk_udp_partial_seq_t *curr_seq = asmblr->sequences;

	if(!asmblr->sequences) return STK_SYSERR;

	while(curr_seq->sequence != seq && curr_seq < asmblr->end_sequence) curr_seq++;
	if(curr_seq >= asmblr->end_sequence) return STK_SYSERR;

	return stk_reassembler_del_partial_sequence(asmblr, curr_seq);
}

void stk_remove_expired_sequences(stk_udp_assembler_t *asmblr)
{
	stk_ret rc = STK_SUCCESS;
	struct timeval curr_time,expire_time;

	gettimeofday(&curr_time,NULL);
	timersub(&curr_time, &asmblr->opts.expiration_interval, &expire_time);

	for(stk_udp_partial_seq_t *pseq = asmblr->sequences; pseq < asmblr->end_sequence; pseq++) {
		if( timercmp(&pseq->create_time,&expire_time,<) ) {
			rc = stk_reassembler_del_partial_sequence(asmblr, pseq);
			STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"deleting expired sequence failed %p assembler %p",pseq->sequence,asmblr);
		}
	}
}

void stk_seq_expiration_cb(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type)
{
	stk_udp_assembler_t *asmblr = (stk_udp_assembler_t *) userdata;

	STK_DEBUG(STKA_NET,"Sequence expiration timer running for assembler %p",asmblr);
	stk_remove_expired_sequences(asmblr);

	if(cb_type == STK_TIMER_EXPIRED && (asmblr->end_sequence - asmblr->sequences)) {
		stk_ret rc = stk_reschedule_timer(timer_set,timer);
		STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"reschedule reconnect timer for tcp client %p",userdata);
	}
}

stk_ret stk_reassembler_add_sequence(stk_udp_assembler_t *asmblr, stk_sequence_t *seq, stk_uint32 unique_id)
{
	int seq_table_sz = (int) (asmblr->end_sequence - asmblr->sequences) + 1;

	STK_UDP_DBG("reassembler save seq %p n %p end %p table sz %d",seq,asmblr->sequences,asmblr->end_sequence,seq_table_sz);
	asmblr->sequences = realloc(asmblr->sequences,seq_table_sz * sizeof(asmblr->sequences[0]));
	if(!asmblr->sequences) return STK_MEMERR;

	asmblr->end_sequence = asmblr->sequences + (seq_table_sz - 1);

	asmblr->end_sequence->sequence = seq;
	asmblr->end_sequence->unique_id = unique_id;
	asmblr->end_sequence->num_rcvd_fragments = 0;
	asmblr->end_sequence->num_segments = 0;
	asmblr->end_sequence->segments = NULL;
	/* asmblr->end_sequence->create_time set in caller */
	asmblr->end_sequence++;
	return STK_SUCCESS;
}

stk_udp_partial_seq_t *stk_reassembler_find(stk_udp_assembler_t *asmblr, stk_sequence_id seq_id,stk_generation_id gen_id, stk_uint32 unique_id)
{
	stk_udp_partial_seq_t *pseq = asmblr->sequences;
	STK_UDP_DBG("reassembler find n %p end %p", asmblr->sequences, asmblr->end_sequence);
	if(pseq == NULL) return NULL;

	do {
		stk_sequence_id stid = stk_get_sequence_id(pseq->sequence);
		stk_generation_id gid = stk_get_sequence_generation(pseq->sequence);
		if(seq_id == stid && gen_id == gid && pseq->unique_id == unique_id) return pseq;
	} while(++pseq < asmblr->end_sequence); 
	STK_UDP_DBG("Failed to find sequence %ld gen %d", seq_id, gen_id);
	return NULL;
}

stk_ret stk_complete_sequence_with_rcvd_data(stk_udp_partial_seq_t *pseq,stk_sequence_t *seq,stk_udp_listener_t *ts,int idx)
{
	stk_udp_assembler_t *asmblr = &ts->asmblr;
	char *data = malloc(pseq->segments[idx].seg_hdr.len);
	stk_uint32 expected_offset = 0;
	stk_ret rc;

	/* Reassemble, remove segments and add to sequence */
	do {
		/* Search for a matching segment to copy */
		idx = pseq->num_segments - 1;

		while(idx > 0 && 
			(pseq->segments[idx].seg_hdr.offset != expected_offset || !pseq->segments[idx].data_len)) idx--;

		memcpy(&data[pseq->segments[idx].seg_hdr.offset],
			pseq->segments[idx].data, pseq->segments[idx].data_len);

		expected_offset = expected_offset + pseq->segments[idx].data_len;

		/* Mark this segment as free */
		pseq->segments[idx].data_len = 0; 
	} while(expected_offset < pseq->segments[idx].seg_hdr.len);

	rc = stk_copy_to_sequence(seq,data,
		pseq->segments[idx].seg_hdr.len, pseq->segments[idx].seg_hdr.type);
	STK_CHECK(STKA_NET,rc==STK_SUCCESS,"Add reassembled data for segment %lu to sequence %p",
		pseq->segments[idx].seg_hdr.type,seq);

	return STK_SUCCESS;
}

stk_ret stk_update_sequence_with_rcvd_data(stk_udp_partial_seq_t *pseq,stk_sequence_t *seq,stk_udp_listener_t *ts)
{
	stk_udp_assembler_t *asmblr = &ts->asmblr;
	stk_udp_wire_read_buf_t *rawudp_bufread = asmblr->raw_bufread;
	stk_udp_wire_fragment_hdr_t *hdr = (stk_udp_wire_fragment_hdr_t *) rawudp_bufread->buf;
	char *nexthdr = (char *) hdr;

	STK_UDP_WIRE_DBG("WIRE:    HDR:    seq id %16lx num_fragments %3ld fragment_idx %3ld generation %3d unique_id %d flags %8x",
		hdr->seq_id, hdr->num_fragments, hdr->fragment_idx, hdr->seq_generation, hdr->unique_id, hdr->flags);

	if(pseq) pseq->num_rcvd_fragments++;

	nexthdr += sizeof(*hdr);
	if(hdr->fragment_idx == 0) {
		stk_udp_wire_fragment0_hdr_t *zh = (stk_udp_wire_fragment0_hdr_t *) nexthdr;
		stk_uint16 nlen;

		STK_UDP_WIRE_DBG("WIRE:   ZHDR: total len %16ld seq_type %8x",zh->total_len, zh->seq_type);

		/* FIXME new API to set sequence generation implemented, but this comment was here: how does tcp set the generation id?? bug/hack?? */
		/* stk_set_sequence_id(seq,hdr->seq_id); - this is set in the caller with the generation id */
		stk_set_sequence_type(seq,zh->seq_type);
		nexthdr += sizeof(*zh);
		nlen = *((stk_uint16 *) nexthdr);
		nexthdr += sizeof(nlen);
		if(nlen > 0) {
			stk_set_sequence_name(seq,strdup(nexthdr));
			nexthdr += nlen;
		}
	}

	while(nexthdr < &rawudp_bufread->buf[rawudp_bufread->read])
	{
		stk_udp_wire_seqment_hdr_t *seg_hdr = (stk_udp_wire_seqment_hdr_t *) nexthdr;
		nexthdr += sizeof(*seg_hdr);

		STK_UDP_WIRE_DBG("WIRE: SEGHDR:       idx %16d offset %10d len %12d type %9lx",
			seg_hdr->idx,seg_hdr->offset,seg_hdr->len,seg_hdr->type);

		if(seg_hdr->offset == 0 && /* Start of a new segment - what if a later frag already arrived ? */
			((hdr->num_fragments == 1) || /* this is simply an optimization */
				(seg_hdr->len <= rawudp_bufread->read - (nexthdr - rawudp_bufread->buf /* curr offset */))))
		{
			/* Everything is contained in this frag, have all the data, create the segment */
			stk_ret rc = stk_copy_to_sequence(seq,nexthdr,seg_hdr->len,seg_hdr->type);
			if(rc!=STK_SUCCESS) {
				STK_LOG(STK_LOG_ERROR,"Failed to copy received data to sequence id %lu",hdr->seq_id);
				return rc;
			}
			nexthdr += seg_hdr->len;
		} else { /* handle multiple frags and a segment is split across them */
			STK_ASSERT(STKA_NET,pseq != NULL,"partial sequence is NULL when trying to handle multiple fragments");

			stk_udp_save_segment(asmblr,pseq,seq,seg_hdr,nexthdr - rawudp_bufread->buf /* curr offset */ );

			STK_UDP_DBG("update seq seg hdr offset %d len %d num_segments %d",seg_hdr->offset,seg_hdr->len,
				pseq->num_segments);

			/* Start the sequence expiration timer for this reassembler */
			if(!asmblr->seq_expiration_timer) {
				asmblr->seq_expiration_timer = stk_schedule_timer(stk_udp_listener_timers,stk_seq_expiration_cb,0,asmblr,DEFAULT_SEQ_EXPIRATION_IVL);
				STK_ASSERT(STKA_NET,asmblr->seq_expiration_timer!=NULL,"start sequence expiration timer for assembler %p",asmblr);
			}

			/* Search for the existing segment, if it exists already */
			if(pseq->num_segments > 1) {
				stk_uint32 expected_offset = 0;

				/* Figure out if we now have enough segments to form the entire thing */
				if(expected_offset >= asmblr->low_segment_offset) {
					/* Search backwards for the expected offset, to set curr segment and get len */
					int idx;
					int segment_complete = 1;

					/* keep searching for offsets adding lens until total is reached or offset is not found */
					do {
						idx = pseq->num_segments - 1;

						while(idx >= 0 && 
							(pseq->segments[idx].seg_hdr.offset != expected_offset || !pseq->segments[idx].data_len)) idx--;
						if(idx < 0) {
							/* Did not find offset */
							segment_complete = 0;
							break;
						}

						expected_offset += pseq->segments[idx].data_len;
					} while(expected_offset < pseq->segments[idx].seg_hdr.len);
					STK_UDP_DBG("RCV segment complete? complete %d",segment_complete);
					if(segment_complete)
						stk_complete_sequence_with_rcvd_data(pseq,seq,ts,idx);
				}
			}
			nexthdr += (seg_hdr->len - seg_hdr->offset);
		}
	}

	STK_UDP_DBG("RCV fragment complete? %d rcvd %ld num %ld",pseq ? pseq->num_rcvd_fragments == hdr->num_fragments : 0,
		pseq ? pseq->num_rcvd_fragments : 0, hdr->num_fragments);
	if(hdr->num_fragments == 1)
		return STK_SUCCESS;
	else
	if(pseq && pseq->num_rcvd_fragments == hdr->num_fragments) {
		/* For now, freeing everything saved. Long term, this is not viable
		 * because many sequences may be stored but for initial testing it'll work.
		 */
		free(pseq->segments);
		pseq->segments = NULL;
		pseq->num_segments = 0;
		return STK_SUCCESS;
	} else
		return STK_INCOMPLETE;
}

static int loss_rate = -1;
static int loss_idx = 0;
static int loss_rate_type = 0;

stk_sequence_t *stk_udp_listener_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	stk_udp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	stk_udp_wire_read_buf_t rawudp_bufread;
	stk_env_t *stkbase = stk_env_from_data_flow(df);

	if(stk_rawudp_listener_recv(ts->rawudp_df,&rawudp_bufread) == 0)
		return NULL;

	if(loss_rate == -1) { /* Technically, not reentrant */
		char *rate = getenv("UDP_LOSS_RATE");
		if(rate && rate[0] == '%') {
			loss_rate = atoi(&rate[1]);
			loss_rate_type = 1;
		} else
			loss_rate = 0;
		/* loss_rate = rate ? atoi(rate) : 0; */
	}

	if(loss_rate > 0) {
		/* Loss rate is set, calculate whether to drop this received packet */
		int old = STK_ATOMIC_INCR(&loss_idx);
		/* Whats the better (more linear) way of doing this??? */
		/* if(old % 100 < loss_rate) return NULL; */
		if(loss_rate_type == 1 && old % loss_rate == 0) {
			STK_UDP_DBG("DROP %d",old);
			return NULL;
		}
	}

	ts->asmblr.raw_bufread = &rawudp_bufread;

	/* Received some raw data, start to deserialize it, find any partially constructed sequence for it and reassemble */
	{
	stk_udp_wire_fragment_hdr_t *hdr = (stk_udp_wire_fragment_hdr_t *) ts->asmblr.raw_bufread->buf;
	stk_sequence_t *seq = NULL;
	stk_udp_partial_seq_t *pseq;
	stk_ret rc;

	/* make the reassembler based on the client IP and port */
	if(hdr->num_fragments == 1 || ((pseq = stk_reassembler_find(&ts->asmblr,hdr->seq_id,hdr->seq_generation,hdr->unique_id)) == NULL)) {
		stk_options_t seq_opts[] = { { "generation", (void *) (stk_uint64) hdr->seq_generation}, {NULL, NULL} };
		seq = stk_create_sequence(stkbase,NULL,hdr->seq_id,0,0,seq_opts);
		stk_reassembler_add_sequence(&ts->asmblr,seq,hdr->unique_id);
		pseq = (&ts->asmblr)->end_sequence - 1;
		gettimeofday(&pseq->create_time,NULL);
		STK_UDP_DBG("RCV new seq %p",seq);
	}
	else {
		if(pseq) seq = pseq->sequence;
		else seq = data_sequence;
		STK_UDP_DBG("RCV reusing seq %p",seq);
	}

	rc = stk_update_sequence_with_rcvd_data(pseq,seq,ts);
	if(rc != STK_SUCCESS && rc != STK_INCOMPLETE) {
		stk_destroy_sequence(seq);
		return NULL;
	}

	ts->asmblr.stats.num_rcvd_fragments++;

	if(rc == STK_SUCCESS) {
		STK_UDP_DBG("Complete Sequence: adding client IP to seq %p",seq);
		rc = stk_rawudp_listener_add_client_ip(df,seq,ts->asmblr.raw_bufread);
		if(rc != STK_SUCCESS) {
			STK_LOG(STK_LOG_ERROR,"update the client IP for a sequence from udp fd %d for data flow %s[%lu], env %p rc %d",
				stk_udp_listener_fd(df),stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
			return NULL;
		}

		rc = stk_data_flow_add_client_protocol(seq,stk_udp_listener_data_flow_protocol(df));
		if(rc != STK_SUCCESS) {
			STK_LOG(STK_LOG_ERROR,"update the client protocol for a sequence from udp fd %d for data flow %s[%lu], env %p rc %d",
				stk_udp_listener_fd(df),stk_data_flow_name(df),stk_get_data_flow_id(df),stk_env_from_data_flow(df),rc);
			return NULL;
		}

		STK_UDP_DBG("Complete Sequence: removing seq %p",seq);
		stk_reassembler_del_sequence(&ts->asmblr, seq);
		ts->asmblr.stats.complete_sequences++;
	} else {
		STK_UDP_DBG("Incomplete Sequence");
		seq = NULL;
	}
	stk_log_reassembler_stats(&ts->asmblr);
	return seq;
	}
}

stk_ret stk_udp_listener_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	stk_udp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	return stk_rawudp_listener_data_flow_id_ip(ts->rawudp_df,data_flow_id,addrlen);
}

stk_ret stk_udp_listener_data_flow_clientip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	stk_udp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	return stk_rawudp_listener_data_flow_clientip(ts->rawudp_df,data_flow_id,addrlen);
}

stk_ret stk_udp_listener_data_flow_buffered(stk_data_flow_t *df)
{
	stk_udp_listener_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	return !STK_SUCCESS;
}

char *stk_udp_listener_data_flow_protocol(stk_data_flow_t *df) { return "udp"; }

stk_ret stk_log_reassembler_stats(stk_udp_assembler_t *asmblr)
{
	STK_DEBUG(STKA_NET_STATS, "Assembler Stats: Total frags rcvd %ld sequences completed %lu sequences stored %lu",
			asmblr->stats.num_rcvd_fragments,asmblr->stats.complete_sequences,
			asmblr->end_sequence - asmblr->sequences);
	return STK_SUCCESS;
}

