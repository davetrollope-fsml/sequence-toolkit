#ifndef STK_ASSERT_LOG_H
#define STK_ASSERT_LOG_H
#include "stdio.h"

extern FILE * stk_assert_log_file;
extern int stk_assert_log;
extern unsigned int stk_assert_val;
extern int stk_assert_flush;
extern int stk_stderr_level;

/* Assert masks */
#define STKA_MEM       0x00000001
#define STKA_SEQ       0x00000002
#define STKA_NET       0x00000004
#define STKA_DF        0x00000008
#define STKA_SVC       0x00000010
#define STKA_SVCGRP    0x00000020
#define STKA_SVCAUT    0x00000040
#define STKA_HTTPD     0x00000080
#define STKA_TIMER     0x00000100
#define STKA_SMB       0x00000200
#define STKA_OPTS      0x00000400
#define STKA_SYNC      0x00000800
#define STKA_NS        0x00001000
#define STKA_API       0x00002000
#define STKA_BIND      0x00004000
#define STKA_DAEMON    0x00008000
#define STKA_TEST      0x10000000
#define STKA_LOG       0x20000000
#define STKA_HEX       0x40000000
#define STKA_NET_STATS 0x80000000

#define STKA_NAME_MAP \
	{ STKA_MEM, "mem" }, \
	{ STKA_SEQ, "seq" }, \
	{ STKA_NET, "net" }, \
	{ STKA_DF, "df" }, \
	{ STKA_SVC, "svc" }, \
	{ STKA_SVCGRP, "svcgrp" }, \
	{ STKA_SVCAUT, "svcaut" }, \
	{ STKA_HTTPD, "httpd" }, \
	{ STKA_TIMER, "timer" }, \
	{ STKA_SMB, "smb" }, \
	{ STKA_OPTS, "opts" }, \
	{ STKA_SYNC, "sync" }, \
	{ STKA_NS, "ns" }, \
	{ STKA_API, "api" }, \
	{ STKA_BIND, "bind" }, \
	{ STKA_DAEMON, "daemon" }, \
	{ STKA_TEST, "test" }, \
	{ STKA_LOG, "log" }, \
	{ STKA_HEX, "hex" }, \
	{ STKA_NET_STATS, "net_stats" }, \
	{ 0, NULL }




void stk_assert_init();
#define STK_ASSERT_LOG_COMMON(_file,_expr,_result,...) do { \
	struct timeval ltv; \
	pthread_t tid = pthread_self(); \
	gettimeofday(&ltv,NULL); \
	fprintf(_file,"%lu:%5d.%06d:%s[%d]:"#_expr ":%s: ",(unsigned long)tid,(int)(ltv.tv_sec % 86400),(int)(ltv.tv_usec),__FUNCTION__, __LINE__, __FILE__); \
	fprintf(_file,__VA_ARGS__); \
	if(!(_result)) fprintf(_file," ** FAILED **"); \
	fprintf(_file,"\n"); \
	if(stk_assert_flush) fflush(_file); \
} while(0)

/* use of this macro outside STK_ASSERT2 assumes stk_assert_init() is called. If not sure, log something. */
#define STK_DEBUG_FLAG(_mask) (stk_assert_log == 1 && (stk_assert_val & _mask))

/* This has grown to have some duplication - can probably be optimized down some */
#define STK_ASSERT2(_fatal,_mask,_expr,...) do { \
	int logged=0; \
	int _result = _expr; /* avoid volatile causing problems in expressions */ \
	if(stk_assert_log == 0) stk_assert_init(); \
	if(STK_DEBUG_FLAG(_mask)) { \
		if(stk_assert_log_file != NULL) { \
			STK_ASSERT_LOG_COMMON(stk_assert_log_file,_expr,_result,__VA_ARGS__); \
			logged = 1; \
		} \
	} \
	if(!(_result)) { \
		if(!logged) \
			STK_ASSERT_LOG_COMMON(stderr,_expr,_result,__VA_ARGS__); \
		if(_fatal) assert(_expr); \
	} \
} while(0)

/* Fatal assertion */
#define STK_ASSERT(_mask,_expr,...) STK_ASSERT2(1,_mask,_expr,__VA_ARGS__) 

/* Non-Fatal assertion */
#define STK_CHECK(_mask,_expr,...) STK_ASSERT2(0,_mask,_expr,__VA_ARGS__) 

#define STK_CHECK_RET(_mask,_expr,_rc,...) do { STK_ASSERT2(0,_mask,_expr,__VA_ARGS__); \
	if(!(_expr)) return (_rc); \
} while(0)

#define STK_DEBUG(_mask,...) STK_ASSERT2(0,_mask,STK_TRUE,__VA_ARGS__) 
#define STK_API_DEBUG() STK_ASSERT2(0,STKA_API,STK_TRUE,"API") 

#define STK_LOG(_level,...) do { \
	const int log = 1; \
	if(_level >= stk_stderr_level) { \
		fprintf(stderr,__VA_ARGS__); \
		fprintf(stderr,"\n"); \
	} \
	STK_ASSERT(STKA_LOG,log,__VA_ARGS__); /* Use log to indicate this is a log and not an assert! */ \
	} while(0)

#endif
