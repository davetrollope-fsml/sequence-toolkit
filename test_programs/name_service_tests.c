/*
 * This file implements a basic client for the name service
 */
#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include "stk_env_api.h"
#include "stk_service_api.h"
#include "stk_sequence_api.h"
#include "eg_dispatcher_api.h"
#include "stk_name_service.h"
#include "stk_name_service_api.h"
#include "stk_test.h"


/* Simple stat */
void process_data(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq)
{
	stk_ret rc = stk_name_service_invoke(rcv_seq);
	TEST_ASSERT(rc==STK_SUCCESS,"invoke name service on sequence");
}

void fd_created_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int added = dispatch_add_fd(default_dispatcher(),flow,fd,NULL,process_data);
	TEST_ASSERT(added != -1,"Failed to add data flow to dispatcher");
}

void fd_destroyed_cb(stk_data_flow_t *flow,stk_data_flow_id id,int fd)
{
	int removed = dispatch_remove_fd(default_dispatcher(),fd);
	TEST_ASSERT(removed != -1,"Failed to remove data flow from dispatcher");
}

typedef struct {
	char protocol[STK_NAME_MAX_PROTOCOL_LEN];
	char ip[16];
	short port;
} name_test_ip_data_t;

typedef struct {
	name_test_ip_data_t ip[2];
	stk_name_ft_state_t ft_state;
	int cbs_rcvd;
	int responses_rcvd;
	int meta_data_count;
	int subscribed_cbs;
	stk_uint64 meta_data_type;
	char meta_data_val[100];
	int expired;
} name_test_data_t;

void test_registration_response_cb(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info_vptr,stk_name_info_cb_type cb_type)
{
	if(cb_type == STK_NS_REQUEST_EXPIRED)      /* Callback is the expiration of a request */
		printf("Registration response expired for name %s user data %d\n",name_info->name,(int) app_info_vptr);
	else {
		name_test_data_t *app_info = (name_test_data_t *) app_info_vptr;
		if(app_info)
			app_info->responses_rcvd++;
		printf("Received registration response on name %s user data %p responses received %d\n",name_info->name,app_info,app_info ? app_info->responses_rcvd : 1);
	}
}

void test_no_meta_data_cb(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info_vptr,stk_name_info_cb_type cb_type)
{
	name_test_data_t *app_info = (name_test_data_t *) app_info_vptr;
	TEST_ASSERT(app_info!=NULL,"No app info in name callback");

	if(cb_type == STK_NS_REQUEST_RESPONSE)     /* Callback is a response from the name server */
	{
		app_info->ft_state = name_info->ft_state;

		for(int i = 0; i < 2; i++)
		{
			char *p = (char *) inet_ntop(AF_INET, &name_info->ip[i].sockaddr.sin_addr, app_info->ip[i].ip, sizeof(app_info->ip[0].ip));

			app_info->ip[i].port = name_info->ip[i].sockaddr.sin_port;
			strcpy(app_info->ip[i].protocol,name_info->ip[i].protocol);

			if(p) 
				printf("Received info on name %s, IP %s Port %d FT State %d\n",
					name_info->name,app_info->ip[i].ip,name_info->ip[i].sockaddr.sin_port,name_info->ft_state);
		}

		app_info->cbs_rcvd++;
	}
	else
	if(cb_type == STK_NS_REQUEST_EXPIRED)      /* Callback is the expiration of a request */
	{
		app_info->expired = 1;
		printf("Expired callback executed for name %s\n",name_info->name);
	}
	else
	printf("unexpected cb_type %d called in test_no_meta_data_cb\n",cb_type);
}

void test_subscription_data_cb(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info_vptr,stk_name_info_cb_type cb_type)
{
	printf("Subscription callback called\n");

	name_test_data_t *app_info = (name_test_data_t *) app_info_vptr;
	app_info->subscribed_cbs++;
}

stk_ret print_meta_data_cb(stk_sequence_t *seq, void *data, stk_uint64 sz, stk_uint64 user_type, void *clientd)
{
	name_test_data_t *app_info = (name_test_data_t *) clientd;
	printf("Meta data type %lu sz %lu\n",user_type,sz);
	if(user_type > 0x100) {
		app_info->meta_data_count++;
		app_info->meta_data_type = user_type;
		strcpy(app_info->meta_data_val,data);
	}
	return STK_SUCCESS;
}

void test_meta_data_cb(stk_name_info_t *name_info,int name_count,void *server_info,void *app_info_vptr,stk_name_info_cb_type cb_type)
{
	name_test_data_t *app_info = (name_test_data_t *) app_info_vptr;
	TEST_ASSERT(app_info!=NULL,"No app info in name callback");

	if(cb_type == STK_NS_REQUEST_EXPIRED)      /* Callback is the expiration of a request */
	{
		app_info->expired = 1;
		printf("Expired callback executed for name %s\n",name_info->name);
		return;
	}

	{
	char *p = (char *) inet_ntop(AF_INET, &name_info->ip[0].sockaddr.sin_addr, app_info->ip[0].ip, sizeof(app_info->ip[0].ip));

	app_info->ip[0].port = name_info->ip[0].sockaddr.sin_port;
	app_info->ft_state = name_info->ft_state;

	if(p) 
		printf("Received info on name %s, IP %s Port %d FT State %d\n",
			name_info->name,app_info->ip[0].ip,name_info->ip[0].sockaddr.sin_port,name_info->ft_state);
	}

	if(name_info->meta_data) {
		stk_ret rc;

		printf("Name %s has %d meta data elements\n",name_info->name,stk_number_of_sequence_elements(name_info->meta_data));

		rc = stk_iterate_sequence(name_info->meta_data,print_meta_data_cb,app_info);
		if(rc != STK_SUCCESS)
			printf("Error iterating over meta data %d\n",rc);
	}
	app_info->cbs_rcvd++;
}

int main(int argc,char *argv[])
{
	stk_env_t *stkbase;
	stk_sequence_t *seq;
	stk_sequence_t *ret_seq;
	stk_service_t *svc;
	stk_bool rc;

	{
	stk_options_t options[] = { { "inhibit_name_service", (void *)STK_TRUE}, { NULL, NULL } };

	stkbase = stk_create_env(options);
	TEST_ASSERT(stkbase!=NULL,"allocate an stk environment");
	}

	/* And get rid of the environment */
	rc = stk_destroy_env(stkbase);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	/* Create the STK environment setting the connection params and registering various callbacks */
	{
	stk_options_t name_options[] = {  { "connect_address", "127.0.0.1"}, {"connect_port", "20002"}, { "nodelay", NULL},
		{ "fd_created_cb", (void *) fd_created_cb }, { "fd_destroyed_cb", (void *) fd_destroyed_cb }, { NULL, NULL } };

	stkbase = stk_create_env(name_options);
	TEST_ASSERT(stkbase!=NULL,"Failed to allocate an stk environment");
	}

#if 0
goto debug;
#endif
	{
	/* Registering an IP/Port on a name with no meta data and make sure it is returned */
	stk_options_t name_options[] = { { "destination_address", "1.2.3.4"}, {"destination_port", "666"}, {"destination_protocol", "tcp"}, { NULL, NULL } };
	name_test_data_t returned_data;

	memset(&returned_data,0,sizeof(returned_data));

	rc = stk_register_name(stk_env_get_name_service(stkbase), "test_no_meta_data", 1000, 1000, test_registration_response_cb, NULL, name_options);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to register name");

	rc = stk_request_name_info(stk_env_get_name_service(stkbase),"test_no_meta_data", 1000, test_no_meta_data_cb, &returned_data, NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to request name");

	/* Dispatch until we've received the expected number of callbacks */
	while(returned_data.cbs_rcvd < 1) {
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
	}

	TEST_ASSERT(strcmp(returned_data.ip[0].ip,"1.2.3.4") == 0,"Failed to get IP for registered name test_no_meta_data");
	TEST_ASSERT(returned_data.ip[0].port == 666,"Failed to get port for registered name test_no_meta_data");
	TEST_ASSERT(strcmp(returned_data.ip[0].protocol,"tcp") == 0,"Failed to get protocol for registered name test_no_meta_data");
	TEST_ASSERT(returned_data.ft_state == STK_NAME_STANDBY,"Failed to get ft_state for registered name test_no_meta_data");

	/* Now test a subscription on that name */
	memset(&returned_data,0,sizeof(returned_data));

	rc = stk_subscribe_to_name_info(stk_env_get_name_service(stkbase),"test_no_meta_data", test_subscription_data_cb, &returned_data, NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to subscribe to name");

	/* Now add another registration which should be notified through the subscription */
	rc = stk_register_name(stk_env_get_name_service(stkbase), "test_no_meta_data", 1000, 1000, test_registration_response_cb, NULL, name_options);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to register another instance of the name");

	/* Dispatch until we've received the expected number of callbacks */
	while(returned_data.subscribed_cbs < 2) {
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
	}

	TEST_ASSERT(returned_data.subscribed_cbs==2,"Failed to get 2 subscribed callbacks");
	}

	{
	/* Now test subscribing before a registration */
	stk_options_t name_options[] = { { "destination_address", "1.2.3.4"}, {"destination_port", "666"}, {"destination_protocol", "tcp"}, { NULL, NULL } };
	name_test_data_t returned_data;

	memset(&returned_data,0,sizeof(returned_data));

	rc = stk_subscribe_to_name_info(stk_env_get_name_service(stkbase),"test_presubscription", test_subscription_data_cb, &returned_data, NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to subscribe to name");

	/* Now add another registration which should be notified through the subscription */
	rc = stk_register_name(stk_env_get_name_service(stkbase), "test_presubscription", 1000, 1000, test_registration_response_cb, NULL, name_options);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to register another instance of the name");

	/* Dispatch until we've received the expected number of callbacks */
	while(returned_data.subscribed_cbs < 1) {
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
	}

	TEST_ASSERT(returned_data.subscribed_cbs==1,"Failed to get 1 presubscribed callbacks");
	}

	{
	/* Registering an Active IP/Port on a name with no meta data and make sure it is returned */
	stk_options_t name_options[] = { { "destination_address", "1.2.3.4"}, {"destination_port", "668"}, {"destination_protocol", "tcp"},
		{ "fault_tolerant_state", "active" }, { NULL, NULL } };
	name_test_data_t returned_data;

	memset(&returned_data,0,sizeof(returned_data));

	rc = stk_register_name(stk_env_get_name_service(stkbase), "test_active_ft_state", 1000, 1000, test_registration_response_cb, NULL, name_options);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to register name");

	rc = stk_request_name_info(stk_env_get_name_service(stkbase),"test_active_ft_state", 1000, test_no_meta_data_cb, &returned_data, NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to request name");

	/* Dispatch until we've received the expected number of callbacks */
	while(returned_data.cbs_rcvd < 1) {
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
	}

	TEST_ASSERT(strcmp(returned_data.ip[0].ip,"1.2.3.4") == 0,"Failed to get IP for registered name test_no_meta_data");
	TEST_ASSERT(returned_data.ip[0].port == 668,"Failed to get port for registered name test_no_meta_data");
	TEST_ASSERT(strcmp(returned_data.ip[0].protocol,"tcp") == 0,"Failed to get protocol for registered name test_no_meta_data");
	TEST_ASSERT(returned_data.ft_state == STK_NAME_ACTIVE,"Failed to get active ft_state for registered name test_no_meta_data");
	}

	{
	/* Registering multiple IPs on a name and make sure they are returned */
	stk_options_t name_options[] = {
		{ "destination_address", "1.2.3.4"}, {"destination_port", "668"}, {"destination_protocol", "tcp"},
		{ "destination_address", "2.3.4.5"}, {"destination_port", "669"}, {"destination_protocol", "udp"},
		{ "fault_tolerant_state", "active" }, { NULL, NULL } };
	name_test_data_t returned_data;

	memset(&returned_data,0,sizeof(returned_data));

	rc = stk_register_name(stk_env_get_name_service(stkbase), "test_active_ft_state", 1000, 1000, test_registration_response_cb, NULL, name_options);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to register name");

	rc = stk_request_name_info(stk_env_get_name_service(stkbase),"test_active_ft_state", 1000, test_no_meta_data_cb, &returned_data, NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to request name");

	/* Dispatch until we've received the expected number of callbacks */
	while(returned_data.cbs_rcvd < 1) {
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
	}

	TEST_ASSERT(strcmp(returned_data.ip[0].ip,"1.2.3.4") == 0,"Failed to get IP for registered name test_no_meta_data");
	TEST_ASSERT(returned_data.ip[0].port == 668,"Failed to get port for registered name test_no_meta_data");
	TEST_ASSERT(strcmp(returned_data.ip[0].protocol,"tcp") == 0,"Failed to get protocol for registered name test_no_meta_data");
	TEST_ASSERT(strcmp(returned_data.ip[1].ip,"2.3.4.5") == 0,"Failed to get IP for registered name test_no_meta_data");
	TEST_ASSERT(returned_data.ip[1].port == 669,"Failed to get port for registered name test_no_meta_data");
	TEST_ASSERT(strcmp(returned_data.ip[1].protocol,"udp") == 0,"Failed to get protocol for registered name test_no_meta_data");
	TEST_ASSERT(returned_data.ft_state == STK_NAME_ACTIVE,"Failed to get active ft_state for registered name test_no_meta_data");
	}

	/* allow timers from previous tests to expire and dispatch them */
	sleep(1);
	rc = stk_env_dispatch_timer_pools(stkbase,100);

	{
	/* Register a name but sleep before dispatching to test expiration of the request */
	stk_options_t name_options[] = { { "connect_address", "1.2.3.4"}, {"connect_port", "666"}, { NULL, NULL } };
	name_test_data_t returned_data;

	memset(&returned_data,0,sizeof(returned_data));

	rc = stk_register_name(stk_env_get_name_service(stkbase), "test_expiration_no_meta_data", 1000, 1000, test_registration_response_cb, NULL, name_options);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to register name");

	rc = stk_request_name_info(stk_env_get_name_service(stkbase),"test_expiration_no_meta_data", 1000, test_no_meta_data_cb, &returned_data, NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to request name");

	printf("Sleeping for 5 seconds to expire the request\n");
	sleep(5);

	/* Expire the timers before dispatching data to ensure no response is processed */
	printf("Dispatching timers\n");
	rc = stk_env_dispatch_timer_pools(stkbase,100);

	/* Dispatch events - should be no events */
	client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);

	TEST_ASSERT(returned_data.expired == 1,"Did not get an expiration callback");
	}

#if 0
	This test is disabled because the semantics have been changed to consider multiple updates
	to the same name from the same data flow as updates instead of orthogonal registrations.
	Subsequent tests have been updated to reflect this.

	{
	/* Registering the same name multiple times with different IP/Ports should get two callbacks */
	stk_options_t name_options[] = { { "connect_address", "1.2.3.4"}, {"connect_port", "666"}, { NULL, NULL } };
	name_test_data_t returned_data;

	memset(&returned_data,0,sizeof(returned_data));

	returned_data.cbs_rcvd = 0;

	rc = stk_register_name(stk_env_get_name_service(stkbase), "multiple_names", 1000, 1000, test_registration_response_cb, NULL, name_options);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to register name");

	name_options[1].data = "667";

	rc = stk_register_name(stk_env_get_name_service(stkbase), "multiple_names", 1000, 1000, test_registration_response_cb, NULL, name_options);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to register name");

	rc = stk_request_name_info(stk_env_get_name_service(stkbase),"multiple_names", 1000, test_no_meta_data_cb, &returned_data, NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to request name");

	/* Dispatch until we've received the expected number of callbacks */
	while(returned_data.cbs_rcvd < 2) {
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
	}

	TEST_ASSERT(strcmp(returned_data.ip[0].ip,"1.2.3.4") == 0,"Failed to get IP for registered name multiple_names");
	/* Can't know which order they will come back - check for either port */
	TEST_ASSERT(returned_data.ip[0].port == 666 || returned_data.ip[0].port == 667,"Failed to get port for registered name multiple_names");
	}
#endif

	{
	/* Registering metadata with a name */
	stk_options_t name_options[] = { { "connect_address", "1.2.3.4"}, {"connect_port", "668"}, { "meta_data_sequence", NULL }, { NULL, NULL } };
	stk_sequence_t *meta_data_seq = NULL;
	name_test_data_t returned_data;

	memset(&returned_data,0,sizeof(returned_data));

	meta_data_seq = stk_create_sequence(stkbase,"meta data sequence",0,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
	TEST_ASSERT(meta_data_seq!=NULL,"Failed to allocate meta data sequence");

	rc = stk_copy_to_sequence(meta_data_seq,"META DATA",10,0x101);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to copy meta data");

	/* Overwrite the NULL entry with the meta data sequence option */
	name_options[2].data = (void *) meta_data_seq;

	rc = stk_register_name(stk_env_get_name_service(stkbase), "test_meta_data", 1000, 1000, test_registration_response_cb, NULL, name_options);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to register name");

	rc = stk_request_name_info(stk_env_get_name_service(stkbase),"test_meta_data", 1000, test_meta_data_cb, &returned_data, NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to request name");

	/* Dispatch until we've received the expected number of callbacks */
	while(returned_data.cbs_rcvd < 1) {
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
	}

	TEST_ASSERT(strcmp(returned_data.ip[0].ip,"1.2.3.4") == 0,"Failed to get IP for registered name test_meta_data");
	TEST_ASSERT(returned_data.ip[0].port==668,"Failed to get port for registered name test_meta_data");
	TEST_ASSERT(returned_data.ft_state == STK_NAME_STANDBY,"Failed to get ft_state for registered name test_meta_data");
	TEST_ASSERT(returned_data.meta_data_count == 1,"Incorrect number of received meta data items %d expected 1",returned_data.meta_data_count);
	TEST_ASSERT(returned_data.meta_data_type==0x101,"Failed to get the right meta data type %lu for registered name test_meta_data",returned_data.meta_data_type);
	TEST_ASSERT(strcmp(returned_data.meta_data_val,"META DATA")==0,"Failed to the right meta data value %s for registered name test_meta_data",returned_data.meta_data_val);
	client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);

	rc = stk_destroy_sequence(meta_data_seq);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy the test sequence : %d",rc);
	}

	{
	/* Registering the same name multiple times with different IP/Ports but using request_once should get one callback */
	stk_options_t name_options[] = { { "connect_address", "2.3.4.5"}, {"connect_port", "666"}, { NULL, NULL } };
	name_test_data_t returned_data;

	memset(&returned_data,0,sizeof(returned_data));

	returned_data.cbs_rcvd = 0;

	rc = stk_register_name(stk_env_get_name_service(stkbase), "multiple_names_one_cb", 1000, 1000, test_registration_response_cb, &returned_data, name_options);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to register name");

	name_options[1].data = "667";

	rc = stk_register_name(stk_env_get_name_service(stkbase), "multiple_names_one_cb", 1000, 1000, test_registration_response_cb, &returned_data, name_options);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to register name");

	rc = stk_request_name_info_once(stk_env_get_name_service(stkbase),"multiple_names_one_cb", 10000, test_no_meta_data_cb, &returned_data, NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to request name");

	/* Dispatch until we've received the expected number of callbacks */
	while(returned_data.cbs_rcvd < 1 && returned_data.responses_rcvd < 2) {
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
	}
	sleep(1); /* Wait to see if we get more */
	client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);

	TEST_ASSERT(strcmp(returned_data.ip[0].ip,"2.3.4.5") == 0,"Failed to get IP for registered name test_no_meta_data");
	/* Can't know which order they will come back - check for either port */
	TEST_ASSERT(returned_data.ip[0].port == 666 || returned_data.ip[0].port == 667,"Failed to get port for registered name test_no_meta_data");
	TEST_ASSERT(returned_data.cbs_rcvd == 1,"should only be 1 callback for request_name_info_once, saw %d", returned_data.cbs_rcvd);
	}

	{
	/* Registering a name in a group doesn't showup in the default group */
	stk_options_t name_options[] = { { "connect_address", "3.4.5.6"}, {"connect_port", "666"}, 
		{ "group_name", "test_name_service"}, { NULL, NULL } };
	stk_options_t lookup_options[] = { { "group_name", "test_name_service"}, { NULL, NULL } };
	name_test_data_t returned_data;

	memset(&returned_data,0,sizeof(returned_data));

	returned_data.cbs_rcvd = 0;

	rc = stk_register_name(stk_env_get_name_service(stkbase), "group_name_test", 1000, 1000, test_registration_response_cb, NULL, name_options);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to register name");

	rc = stk_request_name_info(stk_env_get_name_service(stkbase),"group_name_test", 1000, test_no_meta_data_cb, &returned_data, lookup_options);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to request name");

	/* Dispatch until we've received the expected number of callbacks */
	while(returned_data.cbs_rcvd < 1) {
		client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);
	}

	TEST_ASSERT(strcmp(returned_data.ip[0].ip,"3.4.5.6") == 0,"Failed to get IP for registered name group_name_teest");
	TEST_ASSERT(returned_data.ip[0].port == 666,"Failed to get port for registered name group_name_test");

	sleep(2);

	/* Verify its not in the default group */
	returned_data.cbs_rcvd = 0;
	printf("Requesting name from default group\n");
	rc = stk_request_name_info(stk_env_get_name_service(stkbase),"group_name_test", 1000, test_no_meta_data_cb, &returned_data, NULL);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to request name");

	sleep(2);

	client_dispatcher_timed(default_dispatcher(),stkbase,NULL,1000);

	TEST_ASSERT(returned_data.cbs_rcvd == 0,"got a response from the default group for group_name_test - shouldn't have, got %d",
		returned_data.cbs_rcvd);
	}
	/* Free the dispatcher (and its related resources) */
	terminate_dispatcher(default_dispatcher());

	/* And get rid of the environment, we are done! */
	rc = stk_destroy_env(stkbase);
	TEST_ASSERT(rc==STK_SUCCESS,"Failed to destroy a stk env object : %d",rc);

	/* Need a test that verifies pending requests are freed on destuction
	 * create name service, send requests with no name server running (UDP?), destroy env
	 */

	printf("%s PASSED\n",argv[0]);
	return 0;
}

