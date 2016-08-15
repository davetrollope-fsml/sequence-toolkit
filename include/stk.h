/** @file stk.h
 * this file is the main header for applicstions to include to use the Sequence Toolkit.
 * It includes all the API headers (named _api.h) for full access. some applications may want
 * to include just the API headers they require, but this header is prpvided for conveniencr.
 */

/** \mainpage
 * Welcome to the Sequence Toolkit API documentation!
 * \section main_intro Introduction
 * The Sequence Toolkit (STK) is a developers toolkit to implement software
 * services, manage them, and help them communicate. 
 * 
 * The STK APIs are organized by *_api.h (function prototypes) and *.h files
 * (typedefs and preprocessor definitions) found in the include dir. Primarily
 * applications will only need to include the relevant *_api.h headers or stk.h
 * which includes all the API headers, which some consider overkill, but is provided
 * as a convenience.
 *
 * The STK APIs are implemented in C and provided as a dynamic library libstk.so in lib
 *
 * Developers are encouraged to start by looking at the 
 * stk_env_api.h, stk_service_api.h and stk_sequence_api.h headers and the simple_client
 * and simple_server examples in the examples dir.
 *
 * For any questions about this API and toolkit, visit http://www.sequence-toolkit.com/
 * and/or send email to support@fsml-technologies.com
 */
#include "stk_common.h"

#include "stk_env_api.h"
#include "stk_sequence_api.h"
#include "stk_service_api.h"
#include "stk_service_group_api.h"
#include "stk_options_api.h"
#include "stk_sg_automation_api.h"
#include "stk_tcp_server_api.h"
#include "stk_tcp_client_api.h"
#include "stk_data_flow_api.h"
#include "stk_smartbeat_api.h"
#include "stk_sync_api.h"
#include "stk_timer_api.h"

