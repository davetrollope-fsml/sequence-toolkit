#ifndef EG_DISPATCHER_API_H
#define EG_DISPATCHER_API_H
#include "stk_env.h"
#include "stk_data_flow.h"

/*
 * This example dispatcher provides an example main loop and is used by the
 * examples. It provides functions which allow dispatching of data received
 * and provides functions to wakeup the dispatcher and to stop dispatching.
 *
 * The Sequence Toolkit does not build in an event loop and applications
 * typically use their own main loop - but this is a good starting point
 * for new applications.
 *
 * Applications must provide a process_data() function. It is expected
 * that customers change/modify this dispatcher as they need.
 */

typedef struct stk_dispatcher_stct stk_dispatcher_t;
typedef void (*fd_hup_cb)(stk_dispatcher_t *d,stk_data_flow_t *flow,int fd);
typedef void (*fd_data_cb)(stk_dispatcher_t *d,stk_data_flow_t *rcvchannel,stk_sequence_t *rcv_seq);

stk_dispatcher_t *alloc_dispatcher();
stk_dispatcher_t *default_dispatcher();
void free_dispatcher(stk_dispatcher_t *d);
void client_dispatcher_timed(stk_dispatcher_t *d,stk_env_t *stkbase,fd_data_cb data_cb,int ms);
void client_dispatcher_hard_timed(stk_dispatcher_t *d,stk_env_t *stkbase,fd_data_cb data_cb,int ms);
void client_dispatcher_poll(stk_dispatcher_t *d,stk_env_t *stkbase,fd_data_cb data_cb);
void wakeup_dispatcher(stk_env_t *env);
void stop_dispatching(stk_dispatcher_t *d);
int dispatch_add_fd(stk_dispatcher_t *d,stk_data_flow_t *df,int fd,fd_hup_cb hup_cb,fd_data_cb data_cb);
int dispatch_remove_fd(stk_dispatcher_t *d,int fd);
void terminate_dispatcher(stk_dispatcher_t *d);
void stk_set_dispatcher_user_data(stk_dispatcher_t *d,void *user_data);
void *stk_get_dispatcher_user_data(stk_dispatcher_t *d);
int server_dispatch_add_fd(stk_dispatcher_t *d,int fd,stk_data_flow_t *df,fd_data_cb data_cb);
void eg_dispatcher(stk_dispatcher_t *d,stk_env_t *stkbase,int max_idle_time);
int dispatch_add_accepted_fd(stk_dispatcher_t *d,int fd,stk_data_flow_t *df,fd_data_cb cb);

#endif
