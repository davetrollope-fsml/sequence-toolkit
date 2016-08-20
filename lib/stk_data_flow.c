#include "stk_data_flow_api.h"
#include "stk_internal.h"
#include "stk_env.h"
#include "stk_env_api.h"
#include "stk_options_api.h"
#include "stk_sequence_api.h"
#include "stk_sync_api.h"
#include <string.h>
#include <ctype.h>

typedef struct stk_data_flow_cbs_stct {
	stk_data_flow_destroyed_cb destroyed_cb;
} stk_data_flow_cbs;

struct stk_data_flow_stct {
	stk_stct_type stct_type;
	stk_env_t *env;
	stk_uint16 flow_type;
	stk_data_flow_module_t fptr;
	char *name;
	stk_data_flow_id id;
	int errno;
	int refcnt;
	stk_data_flow_cbs df_cbs;
	void *module_data;
};

stk_data_flow_t *stk_alloc_data_flow(stk_env_t *env,stk_uint16 flow_type,char *name,stk_data_flow_id id,int extendedsz,stk_data_flow_module_t *fptrs,stk_options_t *options)
{
	stk_data_flow_t *df;

	STK_CALLOC_STCT_EX(STK_STCT_DATA_FLOW,stk_data_flow_t,extendedsz,df);
	if(df) {
		df->flow_type = flow_type;
		df->env = env;
		df->name = name;
		df->id = id;
		df->module_data = ((char *) df) + sizeof(stk_data_flow_t);
		memcpy(&df->fptr,fptrs,sizeof(*fptrs));
		df->df_cbs.destroyed_cb = (stk_data_flow_destroyed_cb) stk_find_option(options,"df_destroyed_cb",NULL);
		df->refcnt = 1;
		return df;
	}
	return NULL;
}

void stk_hold_data_flow(stk_data_flow_t *df)
{
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_hold_data_flow is structure type %d",df,df->stct_type);
	STK_ATOMIC_INCR(&df->refcnt);
}

stk_ret stk_free_data_flow(stk_data_flow_t *df)
{
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_free_data_flow is structure type %d",df,df->stct_type);
	if(STK_ATOMIC_DECR(&df->refcnt) == 1) {
		STK_FREE_STCT(STK_STCT_DATA_FLOW,df);
	}
	return STK_SUCCESS;
}

void *stk_data_flow_module_data(stk_data_flow_t *df) {
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_data_flow_module_data is structure type %d",df,df->stct_type);
	return df->module_data;
}

int stk_data_flow_errno(stk_data_flow_t *df) {
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_data_flow_errno is structure type %d",df,df->stct_type);
	return df->errno;
}

void stk_set_data_flow_errno(stk_data_flow_t *df,int errno) {
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_set_data_flow_errno is structure type %d",df,df->stct_type);
	df->errno = errno;
}

char *stk_data_flow_name(stk_data_flow_t *df) {
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_data_flow_name is structure type %d",df,df->stct_type);
	return df->name;
}

stk_data_flow_id stk_get_data_flow_id(stk_data_flow_t *df) {
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_get_data_flow_id is structure type %d",df,df->stct_type);
	return df->id;
}

stk_uint16 stk_get_data_flow_type(stk_data_flow_t *df)
{
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_get_data_flow_type is structure type %d",df,df->stct_type);
	return df->flow_type;
}

stk_env_t *stk_env_from_data_flow(stk_data_flow_t *df) {
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_env_from_data_flow is structure type %d",df,df->stct_type);
	return df->env;
}

stk_ret stk_destroy_data_flow(stk_data_flow_t *df)
{
	stk_ret rc;

	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_destroy_data_flow is structure type %d",df,df->stct_type);
	if(df->df_cbs.destroyed_cb) df->df_cbs.destroyed_cb(df,df->id);
	if(!df->fptr.destroy_data_flow) return STK_NOT_SUPPORTED;
	return df->fptr.destroy_data_flow(df);
}

stk_ret stk_data_flow_send(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_data_flow_send is structure type %d",df,df->stct_type);
	if(!df->fptr.data_flow_send) return STK_NOT_SUPPORTED;
	return df->fptr.data_flow_send(df,data_sequence,flags);
}

stk_sequence_t *stk_data_flow_rcv(stk_data_flow_t *df,stk_sequence_t *data_sequence,stk_uint64 flags)
{
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_data_flow_rcv is structure type %d",df,df->stct_type);
	if(!df->fptr.data_flow_rcv) return NULL;
	return df->fptr.data_flow_rcv(df,data_sequence,flags);
}

stk_ret stk_data_flow_id_ip(stk_data_flow_t *df,struct sockaddr *data_flow_id,socklen_t addrlen)
{
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_data_flow_id_ip is structure type %d",df,df->stct_type);
	if(!df->fptr.data_flow_id_ip) return STK_NOT_SUPPORTED;
	return df->fptr.data_flow_id_ip(df,data_flow_id,addrlen);
}

stk_ret stk_data_flow_id_ip_nw(stk_data_flow_t *df,struct sockaddr_in *data_flow_id,socklen_t addrlen)
{
	stk_ret getiprc = stk_data_flow_id_ip(df,(struct sockaddr *) data_flow_id,addrlen);
	STK_CHECK_RET(STKA_SMB,getiprc==STK_SUCCESS,getiprc,"Couldn't get IP for df %p",df);

	data_flow_id->sin_addr.s_addr = htonl(data_flow_id->sin_addr.s_addr);
	data_flow_id->sin_port = htons(data_flow_id->sin_port);

	STK_DEBUG(STKA_NET,"IP address %x:%d", ntohl(data_flow_id->sin_addr.s_addr), ntohs(data_flow_id->sin_port));

	return getiprc;
}

char *stk_data_flow_protocol(stk_data_flow_t *df)
{
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_data_flow_protocol is structure type %d",df,df->stct_type);
	if(!df->fptr.data_flow_protocol) return NULL;
	return df->fptr.data_flow_protocol(df);
}

stk_ret stk_data_flow_buffered(stk_data_flow_t *df)
{
	STK_ASSERT(STKA_DF,df->stct_type==STK_STCT_DATA_FLOW,"data flow %p passed in to stk_data_flow_buffered is structure type %d fptr %p",df,df->stct_type,df->fptr.data_flow_buffered);
	if(!df->fptr.data_flow_buffered) return STK_NOT_SUPPORTED;
	return df->fptr.data_flow_buffered(df);
}

stk_ret stk_data_flow_client_ip(stk_sequence_t *seq,struct sockaddr_in *client_ip,socklen_t *addrlen)
{
	struct sockaddr_in *client_ip_ptr;
	stk_uint64 len = (stk_uint64) *addrlen;
	stk_ret rc;

	rc = stk_sequence_find_meta_data_by_type(seq, STK_DATA_FLOW_CLIENTIP_ID, (void **) &client_ip_ptr,&len);
	if(rc == STK_SUCCESS) {
		*addrlen = (int) len;
		memcpy(client_ip,client_ip_ptr,*addrlen);
		client_ip->sin_addr.s_addr = ntohl((unsigned long)client_ip->sin_addr.s_addr);
		client_ip->sin_port = ntohs(client_ip->sin_port);
	}
	return rc;
}

stk_ret stk_data_flow_add_client_ip(stk_sequence_t *seq,struct sockaddr_in *client_ip_ptr,socklen_t addrlen)
{
	struct sockaddr_in client_ip;
	memcpy(&client_ip,client_ip_ptr,addrlen);
	client_ip.sin_addr.s_addr = ntohl((unsigned long)client_ip.sin_addr.s_addr);
	client_ip.sin_port = ntohs(client_ip.sin_port);
	return stk_copy_to_sequence_meta_data(seq,(struct sockaddr *)&client_ip,addrlen,STK_DATA_FLOW_CLIENTIP_ID);
}

stk_ret stk_data_flow_client_protocol(stk_sequence_t *seq,char *protocol_ptr, stk_uint64 *plen)
{
	stk_uint64 len = (stk_uint64) *plen;
	stk_ret rc;
	char *ptr;

	rc = stk_sequence_find_meta_data_by_type(seq, STK_DATA_FLOW_CLIENT_PROTOCOL_ID, (void **) &ptr, &len);
	if(rc == STK_SUCCESS) {
		*plen = len;
		memcpy(protocol_ptr,ptr,(int)*plen);
	}
	return rc;
}

stk_ret stk_data_flow_add_client_protocol(stk_sequence_t *seq,char *protocol)
{
	return stk_copy_to_sequence_meta_data(seq,protocol,strlen(protocol) + 1,STK_DATA_FLOW_CLIENT_PROTOCOL_ID);
}

/* Utility to find a data flow option and do all the necessary work for data flow creation */
stk_data_flow_t *stk_data_flow_process_extended_options(stk_env_t *env, stk_options_t *options, char *option_name, stk_create_data_flow_t create_data_flow)
{
	stk_data_flow_t *df;
	STK_API_DEBUG();

	/* First look to see if a data flow was provided in the options */
	df = stk_find_option(options,option_name,NULL);
	if(df) {
		stk_hold_data_flow(df);
	} else {
		/* Look to see if options were provided and auto create this data flow 
		 * <option_name>_options is an option that would point to a new option chain for a specific data flow
		 */
		char extended_option_name[128];
		stk_options_t *df_options;

		strcpy(extended_option_name,option_name);
		strcat(extended_option_name,"_options");
		df_options = stk_find_option(options,extended_option_name,NULL);
		if(df_options) {
			/* Create a data flow and return it */

			/* Get the data flow name and ID from the chained options */
			char *df_name = stk_find_option(df_options,"data_flow_name",NULL);
			stk_data_flow_id df_id = (stk_data_flow_id) stk_find_option(df_options,"data_flow_id",NULL);

			if(!df_name)
				df_name = "auto created data flow";

			/* In the future, the name can be used to look up existing connections too.
			 * That might be best embedded in the data flow logic. Need to determine. For now just create.
			 */

			df = create_data_flow(env, df_name, df_id, df_options);
			STK_ASSERT(STKA_NET,df!=NULL,"auto create client data flow %s",df_name);
		} else {
			df = stk_env_get_monitoring_data_flow(env);
			if(df)
				stk_hold_data_flow(df);
		}
	}
	return df;
}

void stk_data_flow_parse_protocol_str(stk_protocol_def_t *def,char *str)
{
	char *colon,*curr;
	char elements[3][128];
	int num = 0;

	memset(def,0,sizeof(*def));
	memset(elements,0,sizeof(elements));

	/* Break elements down then decide what each is */
	curr = str;
	while(curr[0] != '\0' && (colon = strchr(curr,':')) && num < 3) {
		strncpy(elements[num],curr,colon - curr);
		elements[num][colon - curr] = '\0';
		curr = colon + 1;
		num++;
	}
	strcpy(elements[num++],curr);
	if((!strcasecmp(elements[0],"udp")) || (!strcasecmp(elements[0],"tcp")) ||
	   (!strcasecmp(elements[0],"rawudp")) || (!strcasecmp(elements[0],"multicast"))) {
		strcpy(def->protocol,elements[0]);
		if(num == 1) return;
		if(isdigit(elements[1][0]))
			strcpy(def->ip,elements[1]);
		else
			strcpy(def->name,elements[1]);
		strcpy(def->port,elements[2]);
	} else
	if(!strcasecmp(elements[0],"lookup")) {
		strcpy(def->protocol,elements[0]);
		strcpy(def->name,elements[1]);
	} else {
		if(isdigit(elements[0][0]))
			strcpy(def->ip,elements[0]);
		else
			strcpy(def->name,elements[0]);
		strcpy(def->port,elements[1]);
	}
}

