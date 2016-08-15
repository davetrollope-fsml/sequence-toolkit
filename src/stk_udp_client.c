#include "stk_data_flow_api.h"
#include "stk_data_flow.h"
#include "stk_common.h"
#include "stk_internal.h"
#include "stk_env_api.h"
#include "stk_sequence.h"
#include "stk_rawudp_api.h"
#include "stk_udp_client_api.h"
#include "stk_udp_internal.h"
#include "stk_options_api.h"
#include "stk_timer_api.h"
#include "stk_sequence_api.h"
#include "stk_sync.h"
#include "stk_udp.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

/* copied from stk_tcp_server.c */
#ifdef __APPLE__
#define STK_NB_SEND_FLAGS 0
#else
#define STK_NB_SEND_FLAGS MSG_NOSIGNAL
#endif

stk_timer_set_t *stk_udp_client_timers;
static int timer_refcount;

stk_ret stk_udp_client_destroy_data_flow(stk_data_flow_t *flow);
stk_ret stk_udp_client_data_flow_send(stk_data_flow_t *flow,stk_sequence_t *data_sequence,stk_uint64 flags);
stk_ret stk_udp_client_data_flow_buffered(stk_data_flow_t *df);
char *stk_udp_client_data_flow_protocol(stk_data_flow_t *df);

static stk_data_flow_module_t udp_client_fptrs = {
	stk_udp_client_create_data_flow, stk_udp_client_destroy_data_flow,
	stk_udp_client_data_flow_send, NULL,
	stk_udp_client_data_flow_id_ip, stk_udp_client_data_flow_buffered,
	stk_udp_client_data_flow_protocol
};

typedef struct stk_udp_client_stct {
	stk_data_flow_t *rawudp_df;
	struct sockaddr_in server_addr;
	stk_uint32 unique_id;
} stk_udp_client_t;

//#define STK_UDP_DBG printf
#ifndef STK_UDP_DBG
#define STK_UDP_DBG(...) 
#endif

stk_data_flow_t *stk_udp_client_create_data_flow(stk_env_t *env,char *name,stk_uint64 id,stk_options_t *options)
{
	stk_data_flow_t *df = stk_alloc_data_flow(env,STK_UDP_CLIENT_FLOW,name,id,sizeof(stk_udp_client_t),&udp_client_fptrs,options);
	stk_udp_client_t *ts = df ? stk_data_flow_module_data(df) : NULL;

	if(!df) return NULL;

	/* Set the client's unique ID */
	ts->unique_id = (stk_uint32) rand();

	/* substitute callbacks for internal callbacks in options?? */

	{
	stk_options_t *extended_options;
	extended_options = stk_copy_extend_options(options, 1);
	stk_ret rc;

	rc = stk_append_option(extended_options, "callback_data_flow", (void *) df);
	STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"append callback data flow to raw listener options");

	ts->rawudp_df = stk_rawudp_client_create_data_flow(env,name,id,extended_options);

	rc = stk_free_options(extended_options);
	STK_ASSERT(STKA_NET,rc==STK_SUCCESS,"free extended options for name service");
	}

	stk_rawudp_client_data_flow_serverip(ts->rawudp_df,(struct sockaddr *) &ts->server_addr,sizeof(ts->server_addr));
	STK_LOG(STK_LOG_NORMAL,"data flow %p %s[%lu] to port %d created (fd %d)",df,stk_data_flow_name(df),stk_get_data_flow_id(df),ntohs(ts->server_addr.sin_port),stk_rawudp_client_fd(ts->rawudp_df));

	return df;
}

stk_ret stk_udp_client_destroy_data_flow(stk_data_flow_t *df)
{
	stk_ret ret;
	stk_udp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */

	STK_API_DEBUG();

	if(ts->rawudp_df) {
		ret = stk_destroy_data_flow(ts->rawudp_df);
		STK_ASSERT(STKA_NET,ret==STK_SUCCESS,"unhook df %p for data flow %p",ts->rawudp_df,df);
	}

	ret = stk_free_data_flow(df);

	return ret;
}

int stk_udp_client_fd(stk_data_flow_t *df)
{
	stk_udp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */

	return stk_rawudp_client_fd(ts->rawudp_df);
}

typedef struct stk_udp_wire_fmt_stct {
	stk_data_flow_t *rawudp_df;
	stk_udp_wire_fragment_hdr_t *hdrs;
	stk_uint64 curr_offset;
	stk_uint64 end_offset;
	stk_uint64 total_len;
	char *seq_name;
	stk_uint16 slen;
	int mru;
	stk_uint64 flags;
	struct sockaddr_in dest_addr;
	char frag[65536];
} stk_udp_wire_fmt_t;

stk_ret stk_udp_calc_fragments_before(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	stk_udp_wire_fmt_t *wirefmt = (stk_udp_wire_fmt_t *) clientd; 
	if(wirefmt->hdrs->num_fragments == 1 && wirefmt->curr_offset == 0) {
		wirefmt->curr_offset += sizeof(*wirefmt->hdrs);
		wirefmt->seq_name = stk_get_sequence_name(seq);
		wirefmt->curr_offset += sizeof(stk_udp_wire_fragment0_hdr_t);
		wirefmt->curr_offset += sizeof(stk_uint16);
		if(wirefmt->seq_name) {
			STK_UDP_DBG("calc frag offset name %lu\n",wirefmt->curr_offset);
			wirefmt->slen = strlen(wirefmt->seq_name) + 1;
			wirefmt->curr_offset += wirefmt->slen;
		}
		wirefmt->total_len = wirefmt->curr_offset;
	}
	return STK_SUCCESS;
}

stk_ret stk_udp_calc_fragments(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	stk_udp_wire_fmt_t *wirefmt = (stk_udp_wire_fmt_t *) clientd; 
	stk_uint64 fraglen;

	while(sz > 0)
	{
		STK_UDP_DBG("calc frag offset loop %lu\n",wirefmt->curr_offset);
		wirefmt->curr_offset += sizeof(stk_udp_wire_seqment_hdr_t);
		if(sz > wirefmt->mru - wirefmt->curr_offset) {
			stk_uint64 partialsz = wirefmt->mru - wirefmt->curr_offset;
			sz -= partialsz;
			wirefmt->total_len += partialsz;
			wirefmt->hdrs->num_fragments++;
			wirefmt->curr_offset = sizeof(*wirefmt->hdrs);
		}
		else {
			wirefmt->curr_offset += sz;
			break; /* Ending this segment in the middle of a fragment */
		}
	}
	STK_UDP_DBG("calc frag offset end %lu\n",wirefmt->curr_offset);
	if(wirefmt->curr_offset == 0)
		wirefmt->total_len += wirefmt->mru;
	else
		wirefmt->total_len += wirefmt->curr_offset;
	wirefmt->end_offset = wirefmt->curr_offset;

	return STK_SUCCESS;
}

stk_ret stk_udp_add_fragment0_hdr(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	stk_udp_wire_fmt_t *wirefmt = (stk_udp_wire_fmt_t *) clientd; 
	stk_udp_wire_fragment0_hdr_t *zh;

	wirefmt->curr_offset += sizeof(*wirefmt->hdrs);
	zh = (stk_udp_wire_fragment0_hdr_t *) &wirefmt->frag[wirefmt->curr_offset];
	zh->total_len = wirefmt->total_len;
	zh->seq_type = stk_get_sequence_type(seq);
	wirefmt->curr_offset += sizeof(stk_udp_wire_fragment0_hdr_t);
	*((stk_uint16 *)&wirefmt->frag[wirefmt->curr_offset]) = wirefmt->slen;
	wirefmt->curr_offset += sizeof(stk_uint16);
	if(wirefmt->slen > 0) {
		STK_UDP_DBG("send frag offset name %lu\n",wirefmt->curr_offset);
		memcpy(&wirefmt->frag[wirefmt->curr_offset],wirefmt->seq_name,wirefmt->slen);
		wirefmt->curr_offset += wirefmt->slen;
	}

	return STK_SUCCESS;
}

stk_ret stk_udp_send_fragments(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	stk_udp_wire_fmt_t *wirefmt = (stk_udp_wire_fmt_t *) clientd; 
	stk_udp_wire_seqment_hdr_t seg_hdr;
	stk_uint64 seg_offset = 0;
	char *cdata = (char *) data;
	stk_ret rc;


	/* Prep segment header, starting at 0 */
	memset(&seg_hdr,0,sizeof(seg_hdr));
	seg_hdr.len = sz;
	seg_hdr.type = user_type;

	while(sz > 0)
	{
		wirefmt->curr_offset += sizeof(seg_hdr);
		if(sz > wirefmt->mru - wirefmt->curr_offset) {
			stk_uint32 seglen;
			seglen = wirefmt->mru - wirefmt->curr_offset;
			memcpy(&wirefmt->frag[wirefmt->curr_offset - sizeof(seg_hdr)],&seg_hdr,sizeof(seg_hdr));

			sz -= seglen;
			memcpy(&wirefmt->frag[wirefmt->curr_offset],&cdata[seg_offset],seglen);
			seg_offset += seglen;

			STK_UDP_DBG("sending seg hdr offset %d len %d frag idx %lu num %lu wire offset %lu end %lu actual len %d\n",
				seg_hdr.offset,seg_hdr.len,
				wirefmt->hdrs->fragment_idx, wirefmt->hdrs->num_fragments - 1,
				wirefmt->curr_offset, wirefmt->end_offset, seglen);

			/* Send fragment */
			rc = stk_rawudp_listener_data_flow_send_dest(wirefmt->rawudp_df,wirefmt->frag,wirefmt->curr_offset + seglen,wirefmt->flags,&wirefmt->dest_addr,sizeof(wirefmt->dest_addr));
			STK_CHECK(STKA_NET,rc == STK_SUCCESS,"Send a full fragment idx %lu",wirefmt->hdrs->fragment_idx);

			wirefmt->hdrs->fragment_idx++;
			wirefmt->curr_offset = sizeof(*wirefmt->hdrs);
			seg_hdr.idx++;
			seg_hdr.offset = seg_offset;
		}
		else {
			memcpy(&wirefmt->frag[wirefmt->curr_offset - sizeof(seg_hdr)],&seg_hdr,sizeof(seg_hdr));
			memcpy(&wirefmt->frag[wirefmt->curr_offset],&cdata[seg_offset],sz);
			wirefmt->curr_offset += sz;

			STK_UDP_DBG("frag idx %lu num %lu offset %lu end %lu sz %ld\n",
				wirefmt->hdrs->fragment_idx, wirefmt->hdrs->num_fragments - 1,
				wirefmt->curr_offset, wirefmt->end_offset, sz);

			if(wirefmt->hdrs->fragment_idx == wirefmt->hdrs->num_fragments - 1 && wirefmt->curr_offset >= wirefmt->end_offset) {
				STK_CHECK(STKA_NET,wirefmt->curr_offset == wirefmt->end_offset,"sz: frag hdr %ld frag0 hdr %ld segment hdr %ld mru %d mru buffer %d",
					sizeof(stk_udp_wire_fragment_hdr_t), sizeof(stk_udp_wire_fragment0_hdr_t),
					sizeof(stk_udp_wire_seqment_hdr_t), wirefmt->mru,65535 - wirefmt->mru);
				STK_ASSERT(STKA_NET,wirefmt->curr_offset == wirefmt->end_offset,"check end of fragment offset matches calculations (curr %ld end %ld diff %ld)",
					wirefmt->curr_offset,wirefmt->end_offset,wirefmt->curr_offset-wirefmt->end_offset);

				STK_UDP_DBG("Sending last frag\n");

				/* Last fragment so send it */
				rc = stk_rawudp_listener_data_flow_send_dest(wirefmt->rawudp_df,wirefmt->frag,wirefmt->curr_offset,wirefmt->flags,&wirefmt->dest_addr,sizeof(wirefmt->dest_addr));
				STK_CHECK(STKA_NET,rc == STK_SUCCESS,"Send a fragment size %lu idx %lu",wirefmt->curr_offset,wirefmt->hdrs->fragment_idx);
			}
			else
				STK_UDP_DBG("partial frag send seg hdr offset %d len %d\n",seg_hdr.offset,seg_hdr.len);

			break; /* Ending this segment in the middle of a fragment */
		}
	}
	STK_UDP_DBG("returning from send Frag offset end %lu\n",wirefmt->curr_offset);

	return STK_SUCCESS;
}

stk_ret stk_udp_send_outstanding_fragment(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	if(!stk_has_any_sequence_elements(seq)) {
		/* Send empty frag! */
		stk_udp_wire_fmt_t *wirefmt = (stk_udp_wire_fmt_t *) clientd; 
		stk_ret rc = stk_rawudp_listener_data_flow_send_dest(wirefmt->rawudp_df,wirefmt->frag,wirefmt->total_len,wirefmt->flags,&wirefmt->dest_addr,sizeof(wirefmt->dest_addr));
		STK_CHECK(STKA_NET,rc == STK_SUCCESS,"Send an empty fragment");
		return rc;
	}
	return STK_SUCCESS;
}

#if 0
UDP Data Flow Send:

TCP builds a vector per segment... Can't use vectors for UDP because sendto doesn't take msghdr which has vectors, and has the to address.. 

Probably good for UDP to contain whole segments. Limiting segments to 64k. Not good..but...if there is flexibility in the headers can expand in the future....

Ordering. Is segment ordering in sequences important? Probably yes.

Create a buffer for each UDP packet to be sent. Would use a lot of memory to desegment in one go before sending anything.
Probably should create an array of headers with pointers to segments and offset in to them. Then iterate over them.
Issues -> How does the receiving side tie together the sequence? IP + Sequence ID + Segment ID? + offset

Wire Segment Header: tcp module commonality?
Sequence ID, number of segments, [segment idx, number of segment fragments, segment fragment #, Name Len, Data Len, Name, Data]... ?
Sequence ID, number of segments, [segment idx, segment len, offset, [Name Len, Name], Data Len, Data]... ?
Sequence ID, [segment idx, segment len, offset, [Name Len, Name], Data Len, Data]... ?

Implement:

	Create a common header for each UDP packet.

	Call stk_sequence_iterate() to calculate the number of fragments needed. look at each segment and its size. calculate header + data and bump header count of fragments.

	Then Call stk_sequence_iterate() to create each UDP packet reusing the header and send it

	Need to create a send buffer API in rawudp
#endif
stk_ret stk_udp_client_data_flow_send(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	stk_udp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */
	stk_ret rc;
	stk_udp_wire_fmt_t wirefmt;

	if((flags & STK_UDP_SEND_FLAG_REUSE_GENID) == 0)
		stk_bump_sequence_generation(data_sequence);

	memset(&wirefmt,0,sizeof(wirefmt));
	wirefmt.mru = 65507;
	wirefmt.hdrs = (stk_udp_wire_fragment_hdr_t *) &wirefmt.frag[0];
	wirefmt.hdrs->num_fragments = 1;
	wirefmt.hdrs->seq_id = stk_get_sequence_id(data_sequence);
	wirefmt.hdrs->seq_generation = stk_get_sequence_generation(data_sequence);
	wirefmt.hdrs->unique_id = ts->unique_id;
	wirefmt.rawudp_df = ts->rawudp_df;
	wirefmt.flags = flags;

	if(ts->server_addr.sin_addr.s_addr == 0) {
		socklen_t addrlen = sizeof(wirefmt.dest_addr);

		rc = stk_data_flow_client_ip(data_sequence,&wirefmt.dest_addr,&addrlen);
		if(rc != STK_SUCCESS) {
			STK_LOG(STK_LOG_ERROR,"Attempt to send on data flow %p '%s'[%lu] without a client IP",df,stk_data_flow_name(df),stk_get_data_flow_id(df));
			return STK_SYSERR;
		}
	}
	else
		memcpy(&wirefmt.dest_addr,&ts->server_addr,sizeof(wirefmt.dest_addr));

	/* Calculate the number of fragments etc and init fragment 0 and common headers */
	rc = stk_iterate_complete_sequence(data_sequence,stk_udp_calc_fragments_before,stk_udp_calc_fragments,NULL,&wirefmt);
	if(rc != STK_SUCCESS) {
		STK_LOG(STK_LOG_ERROR,"iteration failed calculating headers to send, rc %d",rc);
		return rc;
	}
	STK_DEBUG(STKA_NET,"udp send num_fragments %lu total len %lu",wirefmt.hdrs->num_fragments,wirefmt.total_len);

	/* Send all the fragments */
	wirefmt.curr_offset = 0;
	rc = stk_iterate_complete_sequence(data_sequence,
		stk_udp_add_fragment0_hdr,stk_udp_send_fragments,stk_udp_send_outstanding_fragment,&wirefmt);
	if(rc != STK_SUCCESS) {
		STK_LOG(STK_LOG_ERROR,"iteration failed calculating headers to send, rc %d",rc);
		return rc;
	}

	return STK_SUCCESS;
}

stk_ret stk_udp_client_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	return STK_SUCCESS;
}

stk_ret stk_udp_client_data_flow_serverip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	stk_udp_client_t *ts = stk_data_flow_module_data(df); /* Asserts on structure type */

	return stk_rawudp_client_data_flow_serverip(ts->rawudp_df,data_flow_id,addrlen);
}

stk_ret stk_udp_client_data_flow_buffered(stk_data_flow_t *df) { return !STK_SUCCESS; }

char *stk_udp_client_data_flow_protocol(stk_data_flow_t *df) { return "udp"; }

