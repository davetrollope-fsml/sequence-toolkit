#ifndef STK_HTTPCONTENT_H
#define STK_HTTPCONTENT_H
#include "mongoose.h"

typedef int (*stk_content_fptr)(const struct mg_request_info *request_info);
struct stk_url_matches { char *match; stk_content_fptr fptr; };
struct stk_url_prefixes { char *prefix; stk_content_fptr fptr; size_t len;};

#endif
