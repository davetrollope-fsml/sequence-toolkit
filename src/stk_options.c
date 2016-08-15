#include "stk_common.h"
#include "stk_internal.h"
#include "stk_options_api.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void *stk_find_option(stk_options_t *options, char *name, stk_options_t **last)
{
	stk_options_t *curr_option = NULL;
	if(options) {
		if(last && *last && (*last)->name == NULL) return NULL;
		curr_option = last ? (*last ? (*last) + 1 : options) : options;
		while(curr_option->name && strcasecmp(curr_option->name,name))
			curr_option++;
		if(last) *last = curr_option;
		if(curr_option->name)
			return curr_option->data;
		else
			return NULL;
	}
	else
		return NULL;
}

stk_options_t *stk_copy_extend_options(stk_options_t *options, int number_to_extend)
{
	stk_options_t *curr_option = NULL;
	int count;
	if(options) {
		count = number_to_extend;

		if(options) {
			curr_option = options;
			while(curr_option->name) {
				curr_option++;
				count++;
			}
		}
	}
	else
		count = number_to_extend;

	curr_option = calloc(sizeof(stk_options_t),count + 1);
	STK_ASSERT(STKA_OPTS,curr_option!=NULL,"copy and extend options due to memory exhaustion");

	if(options)
		memcpy(curr_option,options,sizeof(stk_options_t) * (count - number_to_extend));
	return curr_option;
}

stk_ret stk_update_option(stk_options_t *options, char *name, void *data, void **old_option)
{
	stk_options_t *curr_option = NULL;
	if(options) {
		curr_option = options;
		while(curr_option->name && strcasecmp(curr_option->name,name))
			curr_option++;
		if(curr_option->name) {
			if(old_option) *old_option = curr_option->data;
			curr_option->data = data;
			return STK_SUCCESS;
		}
	}
	return !STK_SUCCESS;
}

stk_ret stk_append_option(stk_options_t *options, char *name, void *data)
{
	if(options) {
		stk_options_t *curr_option = NULL;

		curr_option = options;
		while(curr_option->name)
			curr_option++;

		curr_option->name = name;
		curr_option->data = data;
		curr_option++;
		curr_option->name = NULL;
		curr_option->data = NULL;
		return STK_SUCCESS;
	}
	else
		return !STK_SUCCESS;
}

stk_options_t *stk_build_options_int(char **strp,int *sz)
{
	char *str = *strp;
	char *curr = str,*end = str + *sz,*key;
	stk_options_t *options = NULL;
	int curr_indent,num_items = 0;
	int new_indent = 0;

	curr_indent = 0;
	while(isspace(*curr) && *curr != '\n' && curr < end) { curr++; curr_indent++; }

	options = calloc(sizeof(stk_options_t),1);

	do {
		num_items++;
		options = realloc(options,sizeof(stk_options_t) * (num_items + 1));
		options[num_items].name = NULL;
		options[num_items].data = NULL;

		key = curr;
		while(!isspace(*curr) && curr < end) curr++;
		if(curr >= end) return options;

		if(*curr == '\n') {
			int subsz;
			*curr = '\0';
			curr++;
			options[num_items - 1].name = strdup(key);
			/* New scope */
			subsz = *sz - (curr-str);
			STK_DEBUG(STKA_OPTS,"Options %p New Scope %s",options,key);
			options[num_items - 1].data = stk_build_options_int(&curr,&subsz);
		} else {
			char *value;

			*curr = '\0';
			curr++;
			while(isspace(*curr) && *curr != '\n' && curr < end) curr++;

			value = curr;
			while(*curr != '\n' && curr < end) curr++;
			*curr = '\0';

			options[num_items - 1].name = strdup(key);
			options[num_items - 1].data = strdup(value);
			STK_DEBUG(STKA_OPTS,"Options %p Key %s Value %s",options,key,value);
			curr++;
		}

		*strp = curr;

		new_indent = 0;
		while(isspace(*curr) && *curr != '\n' && curr < end) { curr++; new_indent++; }
	} while(new_indent >= curr_indent);

	options[num_items].name = NULL;
	options[num_items].data = NULL;
	STK_DEBUG(STKA_OPTS,"Options %p End Scope",options);
	return options;
}

stk_options_t *stk_build_options(char *str,int sz)
{
	return stk_build_options_int(&str,&sz);
}

stk_ret stk_free_built_options(stk_options_t *options)
{
	if(!options) return STK_SUCCESS;

	while(options->name) {
		STK_DEBUG(STKA_OPTS,"free built option %s %p",options->name,options->data);
		if(options->name) {
			free(options->name);
			options->name = NULL;
		}
		if(options->data) {
			free(options->data);
			options->data = NULL;
		}
		options++;
	}
	return STK_SUCCESS;
}

stk_ret stk_free_options(stk_options_t *options)
{
	free(options);
	return STK_SUCCESS;
}

