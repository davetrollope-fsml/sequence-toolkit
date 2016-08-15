#define _GNU_SOURCE

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>

/* Use internal header for asserts */
#include "stk_internal.h"

/* command line options provided - set in process_cmdline() */
static struct cmdopts {
	char *config_file_name;
	short named_run,httpd_run;
} opts;

/* Buffer holding config file which is modified and passed as argc/argv */
char config_buffer[65535];

void stknamed_term(int signum);
void stkhttpd_term(int signum);
static void term(int signum)
{
	printf("stkd received SIGTERM/SIGINT, exiting...\n");
	if(opts.httpd_run) stkhttpd_term(signum);
	if(opts.named_run) stknamed_term(signum);
}

typedef struct args_stct {
	int argc;
	char **argv;
} args_t;

char *stkhttpd_args_data[256] = { "stkhttpd" };
args_t stkhttpd_args = { 1, stkhttpd_args_data };

extern int stk_httpd_main(int shard,int argc,char *argv[]);
void *stkhttpd_start(void *argp)
{
	args_t *args = (args_t *) argp;

	stk_httpd_main(1,args->argc,args->argv);
	return NULL;
}

char *stknamed_args_data[256] = { "stknamed" } ;
args_t stknamed_args = { 1, stknamed_args_data };

extern int stk_named_main(int shard,int argc,char *argv[]);
void *stknamed_start(void *argp)
{
	args_t *args = (args_t *) argp;

	stk_named_main(1,args->argc,args->argv);
	return NULL;
}

extern void stk_named_usage();
extern void stk_httpd_usage();
static void usage()
{
	fprintf(stderr,"Usage: stkd [options]\n");
	fprintf(stderr,"       -h                     : This help!\n");
	fprintf(stderr,"       -c <config file>       : Config file location (default: stkd.cfg)\n");
	fprintf(stderr,"       -H                     : Run httpd (when no present in config)\n");
	fprintf(stderr,"       -N                     : Run named (when no present in config)\n");
	fprintf(stderr,"The config file should comntain command line args for each daemon:\n");
	fprintf(stderr,"named: <args>\n");
	fprintf(stderr,"httpd: <args>\n");
}

char *argvize_line(char *line,int *argc,char **argv)
{
	int in_dquote = 0, in_squote = 0;
	char *curr = line;

	/* turn spaces in to nulls, paying attention to quotes */
	while(*curr) {
		while(isspace(*curr)) curr++;
		*argv++ = curr;
		*argc = *argc + 1;
		while(*curr != '\0' && !isspace(*curr) && !(in_dquote || in_squote)) {
			if(*curr == '\"' && !in_squote) in_dquote = !in_dquote;
			if(*curr == '\'' && !in_dquote) in_squote = !in_squote;
			curr++;
		}
		if(!*curr) break;

		*curr = '\0';
		curr++;
	}
	return ++curr;
}

void parse_config()
{
	char *curr = config_buffer,*end;

	/* Convert to lines */
	for(char *cr = strchr(curr,'\n'); cr; cr = strchr(cr,'\n')) {
		cr[0] = '\0';
		end = ++cr;
	}

	/* daemon name: args */
	do {
		char *colon = strchr(curr,':');

		STK_DEBUG(STKA_DAEMON,"Config line: %s",curr);

		if(colon) {
			if(strncasecmp("httpd",curr,colon - curr) == 0) {
				/* Matched httpd args */
				curr = argvize_line(colon + 2,&stkhttpd_args.argc,&stkhttpd_args_data[1]);
				opts.httpd_run = 1;
			} else
			if(strncasecmp("named",curr,colon - curr) == 0) {
				/* Matched named args */
				curr = argvize_line(colon + 2,&stknamed_args.argc,&stknamed_args_data[1]);
				opts.named_run = 1;
			}
			else
				curr += strlen(curr) + 1;
		}
		else
			curr += strlen(curr) + 1;
	} while(curr < end);
}

stk_ret read_config()
{
	FILE *config = fopen(opts.config_file_name,"r");

	if(config) {
		size_t bread = fread(config_buffer, 1, sizeof(config_buffer), config);
		fclose(config);

		if(bread > 0)
			parse_config();
	}
	return STK_SUCCESS;
}

static int process_cmdline(int argc,char *argv[],struct cmdopts *opts )
{
	int rc;

	while(1) {
		rc = getopt(argc, argv, "hHNc:");
		if(rc == -1) return 0;

		switch(rc) {
		case 'h': /* Help! */
			usage();
			stk_named_usage();
			stk_httpd_usage();
			exit(0);

		case 'c': /* IP/Port of multicast name server*/
			opts->config_file_name = optarg;
			break;

		case 'H': opts->httpd_run = 1; break;
		case 'N': opts->named_run = 1; break;
		}
	}
	return 0;
}

int main(int argc,char *argv[])
{
	int rc;
	stk_ret ret;
	pthread_t stkhttpd_id;
	pthread_t stknamed_id;

	opts.config_file_name = "stkd.cfg";

	/* Get the command line options and fill out opts with user choices */
	if(process_cmdline(argc,argv,&opts) == -1) {
		usage();
		exit(5);
	}

	signal(SIGTERM, term); /* kill */
	signal(SIGINT, term);  /* ctrl-c */

	ret = read_config();
	STK_ASSERT(STKA_DAEMON,ret==STK_SUCCESS,"read config %d",rc);

	printf("stkd running\n");

	if(opts.named_run) {
		optind = 1;
		rc = pthread_create(&stknamed_id, NULL, stknamed_start, &stknamed_args);
		STK_ASSERT(STKA_DAEMON,rc==0,"create stknamed %d",rc);
	}

	if(opts.httpd_run && opts.named_run)
		sleep(1);

	if(opts.httpd_run) {
		optind = 1;
		rc = pthread_create(&stkhttpd_id, NULL, stkhttpd_start, &stkhttpd_args);
		STK_ASSERT(STKA_DAEMON,rc==0,"create stkhttpd %d",rc);
	}

	sleep(60000);

	return 0;
}

