
/*
 * This file implements some basic dispatching routines.
 * It could be more refined but is deliberately not
 * so its operation is clear for the reader and is
 * intended to demonstrate concepts.
 */
#include "stk_env_api.h"
#include "stk_sequence_api.h"
#include "stk_data_flow.h"
#include "stk_tcp_server_api.h"
#include "stk_udp_listener_api.h"
#include "stk_tcp_client_api.h"
#include "stk_data_flow_api.h"
#include "stk_tcp.h"
#include "stk_timer_api.h"
#include "stk_examples.h"
#include "eg_dispatcher_api.h"
#include <poll.h>
#include <sys/time.h>

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>

/* Max number of connections and the array size to store them */
#ifndef MAX_CONNS
#define MAX_CONNS 500
#endif
#define MAX_CONN_ARRAY_SZ (MAX_CONNS + 1) /* Must have space for the wakeup fd, hence +1 */

typedef struct {
	stk_data_flow_t *df; /* The data flow this fd is associated with */
	fd_hup_cb hup_cb;    /* Hangup callback */
	fd_data_cb data_cb;  /* Data callback associated with this fd */ 
	int listening;       /* listen socket - calls accept on */
	int pipe;            /* wakeup pipe */
	int accepted;        /* ephemeral fd */
} fdinfo_t;

struct stk_dispatcher_stct {
	int wakeup_fds[2];                            /* FDs for the wakeup pipe */
	int nfds;
	int end_dispatch;                             /* Flag indicating that the dispatcher should return */
	fdinfo_t fdinfo[MAX_CONN_ARRAY_SZ];
	struct pollfd fdset[MAX_CONN_ARRAY_SZ];       /* The FD set to be passed to poll() */
	void *user_ref;                               /* User data */
	stk_timer_set_t *timer_dispatch_set;
};
stk_dispatcher_t global_dispatcher = { { -1, -1 } }; /* Default dispatcher */

/* Default timeout for poll() - 100ms is a decent compromise
 * timers are processed after poll() times out. Reduce for
 * finer granulariy.
 */
#define DEFAULT_EXPIRATION_TIME 100

stk_dispatcher_t *alloc_dispatcher()
{
	stk_dispatcher_t *d = calloc(sizeof(stk_dispatcher_t),1);
	if(!d) return NULL;

	d->wakeup_fds[0] = -1;
	d->wakeup_fds[1] = -1;
	return d;
}

void free_dispatcher(stk_dispatcher_t *d)
{
	if(d->timer_dispatch_set)
		STK_ASSERT(stk_free_timer_set(d->timer_dispatch_set,STK_FALSE) == STK_SUCCESS,"Failed to free timer set");
	free(d);
}

stk_dispatcher_t *default_dispatcher()
{
	return &global_dispatcher;
}

/* API to set the end dispatch flag */
void stop_dispatching(stk_dispatcher_t *d)
{
	d->end_dispatch = 1;
}

/* Init a specific index in the fdset and fdinfo tables */
void dispatch_init_fdinfo(stk_dispatcher_t *d,int idx,stk_data_flow_t *df,int fd,fd_hup_cb hup_cb,fd_data_cb data_cb)
{
	memset(&d->fdinfo[idx],0,sizeof(d->fdinfo[0]));
	d->fdset[idx].fd = fd;
	d->fdset[idx].events = POLLIN;
	d->fdset[idx].revents = 0;
	d->fdinfo[idx].hup_cb = hup_cb;
	d->fdinfo[idx].data_cb = data_cb;
	d->fdinfo[d->nfds].df = df;
}

/* Add a pipe FD to the dispatcher */
int pipe_dispatch_add_fd(stk_dispatcher_t *d,int fd)
{
	if(d->nfds + 1 == MAX_CONN_ARRAY_SZ) return -1;
	dispatch_init_fdinfo(d,d->nfds,NULL,fd,NULL,NULL);
	d->fdinfo[d->nfds].pipe = 1;
	d->nfds++;
	return 0;
}

/* Function to init the wakeup fds if not yet setup */
inline static void dispatch_init_wakeup_fds(stk_dispatcher_t *d)
{
	/* create a pipe so other threads can wakeup the dispatch loop while its in poll() */
	if(d->wakeup_fds[0] == -1) {
		int rc = pipe(d->wakeup_fds);
		STK_ASSERT(rc!=-1,"failed to create dispatch pipe");

		pipe_dispatch_add_fd(d,d->wakeup_fds[0]);
	}
}

/* Add a generic FD and data flow to the dispatcher */
int dispatch_add_fd(stk_dispatcher_t *d,stk_data_flow_t *df,int fd,fd_hup_cb hup_cb,fd_data_cb data_cb)
{
	if(d->nfds + 1 == MAX_CONN_ARRAY_SZ) return -1;
	dispatch_init_wakeup_fds(d);
	dispatch_init_fdinfo(d,d->nfds,df,fd,hup_cb,data_cb);
	d->nfds++;
	return 0;
}

/* Add a listening FD and data flow to the dispatcher */
int server_dispatch_add_fd(stk_dispatcher_t *d,int fd,stk_data_flow_t *df,fd_data_cb data_cb)
{
	if(d->nfds + 1 == MAX_CONN_ARRAY_SZ) return -1;
	dispatch_init_wakeup_fds(d);
	dispatch_init_fdinfo(d,d->nfds,df,fd,NULL,data_cb);
	d->fdinfo[d->nfds].listening = 1;
	d->nfds++;
	return 0;
}

void dispatch_destroy_accepted_cb(stk_dispatcher_t *d,stk_data_flow_t *flow,int fd)
{
	int removed = dispatch_remove_fd(d,fd);
	STK_ASSERT(removed != -1,"remove data flow from dispatcher");

	/* Force closing of fd on data flow */
	{
	stk_ret ret = stk_destroy_data_flow(flow);
	STK_ASSERT(ret==STK_SUCCESS,"destroy data flow %p",flow);
	}
}

/* Add an accepted FD and data flow to the dispatcher
 * Accepted data flows are allocated by the dispatcher
 * and must be freed by it
 */
int dispatch_add_accepted_fd(stk_dispatcher_t *d,int fd,stk_data_flow_t *df,fd_data_cb cb)
{
	if(d->nfds + 1 == MAX_CONN_ARRAY_SZ) return -1;
	dispatch_init_fdinfo(d,d->nfds,df,fd,dispatch_destroy_accepted_cb,cb);
	d->fdinfo[d->nfds].accepted = 1;
	d->fdinfo[d->nfds].df = df;
	d->nfds++;
	return 0;
}

/* API to call to wakeup a dispatcher. It works by writing 
 * a byte to a pipe which the dispatch is listening to. On
 * receiving data poll() will return indicating there is data
 * and thus the dispatch loop is woken.
 */
void wakeup_dispatcher(stk_env_t *env)
{
	char b = 0;
	ssize_t rc;

	dispatch_init_wakeup_fds(&global_dispatcher);

	rc = write(global_dispatcher.wakeup_fds[1],&b,1);
	STK_ASSERT(rc != -1,"Failed to write byte to wakeup pipe %d",errno);
}

/* Remove a specific fdset/fdinfo index sliding the tables down */
int dispatch_remove_fdidx(stk_dispatcher_t *d,int idx)
{
	if(d->nfds == 0) return -1;

	/* Compress fdset */
	for(int idx2 = idx + 1; idx2 < d->nfds; idx2++) {
		d->fdset[idx2 - 1].fd = d->fdset[idx2].fd;
		d->fdset[idx2 - 1].revents = d->fdset[idx2].revents;
		d->fdinfo[idx2 - 1].hup_cb = d->fdinfo[idx2].hup_cb;
		d->fdinfo[idx2 - 1].df = d->fdinfo[idx2].df;
		d->fdinfo[idx2 - 1].listening = d->fdinfo[idx2].listening;
		d->fdinfo[idx2 - 1].accepted = d->fdinfo[idx2].accepted;
		d->fdinfo[idx2 - 1].pipe = d->fdinfo[idx2].pipe;
	}
	d->nfds--;

	return 0;
}

/* Remove a specific fd from the fdset/fdinfo tables */
int dispatch_remove_fd(stk_dispatcher_t *d,int fd)
{
	for(int idx = 0; idx <= d->nfds; idx++)
		if(d->nfds > idx && d->fdset[idx].fd == fd) {
			dispatch_remove_fdidx(d,idx);
			return idx;
		}
	return 0;
}

/* Kill the dispatcher and close resources (aka the wakeup pipe) */
void terminate_dispatcher(stk_dispatcher_t *d)
{
	close(d->wakeup_fds[1]);
	close(d->wakeup_fds[0]);
	dispatch_remove_fd(d,d->wakeup_fds[0]);
}

/* Function to clear events from the fdset */
void clear_events(stk_dispatcher_t *d)
{
	for(int idx = 0; idx <= MAX_CONNS; idx++) {
		d->fdset[idx].revents = 0;
	}
}

/* An example generic dispatcher that handles timers, server data flows
 * and client data flows.
 * max_idle_time dictates the max time between event loops that this method will sleep.
 *   IMPORTANT: It does not dictate how quickly the function returns, it is really
 *   only relevant when trying to limit time time it takes to detect an exit event
 *   or when trying to use it in a polling mode where max_idle_time is set to 0.
 */
void eg_dispatcher(stk_dispatcher_t *d,stk_env_t *stkbase,int max_idle_time)
{
	int rc;
	int expiration_time = max_idle_time;

	if(max_idle_time == 0)
		d->end_dispatch = 1;
	else
		d->end_dispatch = 0;

	dispatch_init_wakeup_fds(d);

	while(1) {
		/* Determine the time until the next timer will fire */
		expiration_time = stk_next_timer_ms_in_pool(stkbase);
		if(expiration_time == -1)
			expiration_time = max_idle_time;
		else
		{
			stk_ret ret = stk_env_dispatch_timer_pools(stkbase,0);
			STK_ASSERT(ret == STK_SUCCESS,"Failed to dispatch timers: %d",ret);
			if(expiration_time > max_idle_time)
				expiration_time = max_idle_time;
		}

		/* Clear events */
		clear_events(d);

		/* Call poll() to wait for events */
		do {
			if(d->end_dispatch) break;

			rc = poll(d->fdset,d->nfds,expiration_time);
		} while(rc == -1 && errno == EINTR);
		if(d->end_dispatch) break;

		STK_ASSERT(rc>=0,"poll returned error %d %d",rc,errno);

		if(rc == 0) continue; /* Timed out, nothing to check */
		if(rc == -1) continue; /* Error occurred, no FD activity to process */

		/* Iterate over the connections fd's to see if there is data, and process */
		for(int idx = 0; idx < d->nfds; idx++) {
			/* Check for new events on the wakeup pipe */
			if(d->fdinfo[idx].pipe && d->fdset[idx].revents & POLLIN) {
				char b;

				ssize_t rc = read(d->wakeup_fds[0],&b,1);
				STK_ASSERT(rc != -1,"Failed to read byte from wakeup pipe %d",errno);
				continue;
			}

			if(d->fdinfo[idx].listening && d->fdset[idx].revents & POLLIN) {
				stk_data_flow_t *newchannel;
				STK_LOG(STK_LOG_NORMAL,"Data on well known port");
				newchannel = stk_tcp_server_accept(d->fdinfo[idx].df);
				continue;
			}

			if(d->fdset[idx].revents & POLLHUP || d->fdset[idx].revents & POLLNVAL) {
				int fd = d->fdset[idx].fd;

				if(d->fdinfo[idx].df == NULL) {
					STK_LOG(STK_LOG_ERROR,"channel %d is null but event received on fd %d",idx,d->fdset[idx].fd);
					continue;
				}

				if(d->fdinfo[idx].hup_cb)
					d->fdinfo[idx].hup_cb(d,d->fdinfo[idx].df,d->fdset[idx].fd);
				else
					dispatch_remove_fd(d,d->fdset[idx].fd);

				/* Error occurred receiving data from this fd */
				STK_LOG(STK_LOG_NORMAL,"channel %d deleted, fd %d",idx,fd);
				continue;
			}

			if(d->fdset[idx].revents & POLLIN) {
				ssize_t len;
				stk_sequence_t *ret_seq;
				stk_sequence_t *rcv_seq;
				stk_data_flow_t *df = d->fdinfo[idx].df;

				if(d->fdinfo[idx].df == NULL) {
					STK_LOG(STK_LOG_ERROR,"channel %d is null but event received on fd %d",idx,d->fdset[idx].fd);
					continue;
				}
				stk_set_data_flow_errno(df,0);
				do {
					/* Allocate a sequence to receive data */
					rcv_seq = stk_create_sequence(stkbase,"eg_dispatcher",0xfedcba90,STK_SEQUENCE_TYPE_DATA,STK_SERVICE_TYPE_DATA,NULL);
					STK_ASSERT(rcv_seq!=NULL,"Failed to allocate rcv test sequence");

					/* Receive data from this connection */
					ret_seq = stk_data_flow_rcv(df,rcv_seq,0);
					if(ret_seq == NULL) {
/* a df could be removed at this point, so this would access the wrong d->fdinfo */
if(stk_data_flow_errno(df) != 0) break;
						if(d->fdinfo[idx].accepted) {
							/* Error occurred receiving data from this fd, and it was created by the dispatch loop - destroy */
							stk_ret rc;

							rc = stk_destroy_data_flow(d->fdinfo[idx].df);
							STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the live tcp data flow: %d",rc);

							STK_LOG(STK_LOG_NORMAL,"channel %d [%p] deleted",idx,d->fdinfo[idx].df);
							d->fdinfo[idx].df = NULL;
							df = NULL;

							dispatch_remove_fdidx(d,idx);
						}
						rc = stk_destroy_sequence(rcv_seq);
						STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the test sequence : %d",rc);
						break;
					}
					else
					{
						/* Process the data received on this connection */
						if(d->fdinfo[idx].data_cb)
							d->fdinfo[idx].data_cb(d,df,ret_seq);
					}

					/* free the data in the sequence */
					rc = stk_destroy_sequence(rcv_seq);
					STK_ASSERT(rc==STK_SUCCESS,"Failed to destroy the test sequence : %d",rc);
				} while(df && stk_data_flow_buffered(df) == STK_SUCCESS);
				continue;
			}
		}
	}
}

int timedout = 0;

void end_client_dispatch_timer(stk_timer_set_t *timer_set,stk_timer_t *timer,int id,void *userdata,void * user_setdata, stk_timer_cb_type cb_type)
{
	timedout = 1;
	stop_dispatching(userdata);
}

/*
 * Run client dispatching with a minimum time to run (aka pause but keep dispatching)
 * This is achieved by creating a timer set, scheduling a timer and dispatching
 * until the timer fires and terminates dispatching.
 *
 * This function respects stop_dispatching() and will return early if that is called
 *
 * Normally an application would create one timer set and reuse it, but for the
 * purposes of simplicity in this example code we create a new one every time and free it
 */
void client_dispatcher_timed(stk_dispatcher_t *d,stk_env_t *stkbase,fd_data_cb data_cb,int ms)
{
	if(!d->timer_dispatch_set)
		d->timer_dispatch_set = stk_new_timer_set(stkbase,NULL,1,STK_FALSE);

	STK_ASSERT(stk_env_add_timer_set(stkbase,d->timer_dispatch_set) == STK_SUCCESS,"Failed to add timer set to env");

	timedout = 0;
	d->end_dispatch = 0;

	stk_schedule_timer(d->timer_dispatch_set,end_client_dispatch_timer,0,d,ms);

	while(timedout == 0 && d->end_dispatch == 0) eg_dispatcher(d,stkbase,DEFAULT_EXPIRATION_TIME);

	STK_ASSERT(stk_env_remove_timer_set(stkbase,d->timer_dispatch_set) == STK_SUCCESS,"Failed to remove default timer set from env");
}

/*
 * Run client dispatching in a polling mode (it does not sleep)
 */
void client_dispatcher_poll(stk_dispatcher_t *d,stk_env_t *stkbase,fd_data_cb data_cb)
{
	eg_dispatcher(d,stkbase,0);
}

/*
 * Run client dispatching with a minimum time to run (aka pause but keep dispatching)
 * This is achieved by creating a timer set, scheduling a timer and dispatching
 * until the timer fires and terminates dispatching.
 *
 * This function *does not* respect stop_dispatching() and will only return after the given time
 *
 * Normally an application would create one timer set and reuse it, but for the
 * purposes of simplicity in this example code we create a new one every time and free it
 */
void client_dispatcher_hard_timed(stk_dispatcher_t *d,stk_env_t *stkbase,fd_data_cb data_cb,int ms)
{
	stk_timer_set_t *timer_dispatch_set;

	if(!d->timer_dispatch_set)
		d->timer_dispatch_set = stk_new_timer_set(stkbase,NULL,1,STK_FALSE);

	STK_ASSERT(stk_env_add_timer_set(stkbase,d->timer_dispatch_set) == STK_SUCCESS,"Failed to add default timer set to env");

	timedout = 0;

	stk_schedule_timer(d->timer_dispatch_set,end_client_dispatch_timer,0,d,ms);

	while(timedout == 0) eg_dispatcher(d,stkbase,DEFAULT_EXPIRATION_TIME);

	STK_ASSERT(stk_env_remove_timer_set(stkbase,d->timer_dispatch_set) == STK_SUCCESS,"Failed to remove default timer set from env");
}

/* Set the user data for a dispatcher */
void stk_set_dispatcher_user_data(stk_dispatcher_t *d,void *user_data) { d->user_ref = user_data; }

/* Get the user data for a dispatcher */
void *stk_get_dispatcher_user_data(stk_dispatcher_t *d) { return d->user_ref; }
