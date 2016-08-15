#include <stdio.h>
#include "stk_options_api.h"
#include "stk_test.h"
#include <strings.h>

int main(int argc,char *argv[])
{
	{
	stk_options_t options[] = { { "key", "value" }, { "key2", "value2" }, { "key", "value3" }, { NULL, NULL } };
	stk_options_t *last_option;
	void *val;
	val = stk_find_option(options,"key2",NULL);
	TEST_ASSERT(val != NULL && strcmp(val,"value2") == 0,"key2 not found or incorrect : %p",val);
	last_option = NULL;
	val = stk_find_option(options,"key",&last_option);
	TEST_ASSERT(val != NULL && strcmp(val,"value") == 0,"key not found or incorrect : %p",val);
	val = stk_find_option(options,"key",&last_option);
	TEST_ASSERT(val != NULL && strcmp(val,"value3") == 0,"duplicate key not found or incorrect : %p",val);
	last_option = NULL;
	val = stk_find_option(options,"missing_key",&last_option);
	TEST_ASSERT(val == NULL,"missing key found : %p",val);
	}

	{
	stk_options_t *options;
	char test_data[1024];
	strcpy(test_data,
"key1 value1\n\
key2 value2\n\
new_scope\n\
 key1a value1a\n\
 key2a value2a\n\
key3 value3\n\
");
	options = stk_build_options(test_data,strlen(test_data));
	TEST_ASSERT(options!=NULL,"Failed to build basic options");
	TEST_ASSERT(strcmp(options[0].name,"key1") == 0,"key1 does not match");
	TEST_ASSERT(strcmp(options[0].data,"value1") == 0,"value1 does not match");
	TEST_ASSERT(strcmp(options[1].name,"key2") == 0,"key2 does not match");
	TEST_ASSERT(strcmp(options[1].data,"value2") == 0,"value2 does not match");
	TEST_ASSERT(strcmp(options[3].name,"key3") == 0,"key3 does not match");
	TEST_ASSERT(strcmp(options[3].data,"value3") == 0,"value3 does not match");
	TEST_ASSERT(options[4].name == NULL,"null name not found");
	TEST_ASSERT(options[4].data == NULL,"null value not found");
	TEST_ASSERT(strcmp(options[2].name,"new_scope") == 0,"new_scope does not match");
	{
	stk_options_t *subopts = (stk_options_t *) options[2].data;
	TEST_ASSERT(strcmp(subopts[0].name,"key1a") == 0,"key1a does not match");
	TEST_ASSERT(strcmp(subopts[0].data,"value1a") == 0,"value1a does not match");
	TEST_ASSERT(strcmp(subopts[1].name,"key2a") == 0,"key2a does not match");
	TEST_ASSERT(strcmp(subopts[1].data,"value2a") == 0,"value2a does not match");
	TEST_ASSERT(subopts[2].name == NULL,"null name not found");
	TEST_ASSERT(subopts[2].data == NULL,"null value not found");
	}
	TEST_ASSERT(stk_free_built_options(options[2].data)==STK_SUCCESS,"free options");
	TEST_ASSERT(stk_free_built_options(options)==STK_SUCCESS,"free options");
	TEST_ASSERT(stk_free_options(options)==STK_SUCCESS,"free options");
	}


	{
	stk_options_t *options;
	char test_data[1024];
	strcpy(test_data,
"new_scope\n\
 key1a value1a\n\
 key2a value2a\n\
key3 value3\n\
");
	options = stk_build_options(test_data,strlen(test_data));
	TEST_ASSERT(options!=NULL,"Failed to build basic scope options");
	TEST_ASSERT(strcmp(options[0].name,"new_scope") == 0,"new_scope does not match");
	TEST_ASSERT(strcmp(options[1].name,"key3") == 0,"key3 does not match");
	TEST_ASSERT(strcmp(options[1].data,"value3") == 0,"value3 does not match");
	TEST_ASSERT(options[2].name == NULL,"null name not found");
	TEST_ASSERT(options[2].data == NULL,"null value not found");
	{
	stk_options_t *subopts = (stk_options_t *) options[0].data;
	TEST_ASSERT(strcmp(subopts[0].name,"key1a") == 0,"key1a does not match");
	TEST_ASSERT(strcmp(subopts[0].data,"value1a") == 0,"value1a does not match");
	TEST_ASSERT(strcmp(subopts[1].name,"key2a") == 0,"key2a does not match");
	TEST_ASSERT(strcmp(subopts[1].data,"value2a") == 0,"value2a does not match");
	TEST_ASSERT(subopts[2].name == NULL,"null name not found");
	TEST_ASSERT(subopts[2].data == NULL,"null value not found");
	}
	TEST_ASSERT(stk_free_built_options(options[0].data)==STK_SUCCESS,"free options");
	TEST_ASSERT(stk_free_built_options(options)==STK_SUCCESS,"free options");
	TEST_ASSERT(stk_free_options(options)==STK_SUCCESS,"free options");
	}

	printf("%s PASSED\n",argv[0]);
	return 0;
}

