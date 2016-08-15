/** @file stk_common.h
 * This header includes the common definitions required across the Sequence Toolkit
 * and will be required by applications except where stk.h is included
 * (which includes this file).
 */
#ifndef STK_COMMON_H
#define STK_COMMON_H

/* ------- Common error handling definitions ------- */
/* success/error typedef and return codes returned by the STK API */
/**
 * \typedef stk_ret
 * Standard return code from STK APIs.
 * \see STK_SUCCESS
 */
typedef unsigned int stk_ret;

#define STK_SUCCESS 0 /*!< Standard return value for success */

#define STK_FAIL 1 /*!< Unspecified failure */

#define STK_INCOMPLETE 2 /*!< Indicates success, but more to do */

#define STK_INVALID_ARG 3 /*!< Argument incorrect */

/** STK_WOULDBLOCK indicates the operation would have blocked and did not 
 * proceed. The application should retry the operation later.
 */
#define STK_WOULDBLOCK 0x10

/** STK_RESET indicates the operation failed because the object has reset.
 * This is typical when a network connection fails.
 * The application should retry the operation later.
 */
#define STK_RESET 0x11

/** STK_NOT_SUPPORTED indicates the operation is not supported.
 * This indicates the API was called on the type of object that
 * does not support the API E.G. sending data on a receive only
 * data flow.
 */
#define STK_NOT_SUPPORTED 0x12

/** STK_NETERR indicates the operation failed due to an unhandled
 * network error. The application should treat this as a transient
 * error and retry the operation later.
 */
#define STK_NETERR 0x20

/** STK_DATA_TOO_LARGE indicates the operation failed because there
 * was too much data
 */
#define STK_DATA_TOO_LARGE 0x21

/** STK_SYSERR indicates the operation failed due to an unhandled
 * system error. The application should treat this as a significant
 * error, but not necessarily fatal and handle appropriately.
 */
#define STK_SYSERR 0x80

/** STK_MEMERR indicates the operation failed due to a lack of memory.  */
#define STK_MEMERR 0x81

/** STK_MAX_TIMERS indicates the maximum number of timer callbacks was hit.  */
#define STK_MAX_TIMERS 0x82

/** STK_NOT_FOUND indicates an operation was requested on an item that could not be found.  */
#define STK_NOT_FOUND 0x83

#define STK_NO_LICENSE 0x84 /*!< Indicates that a license is required, or license limits exceeded */


/* ------- Common logging level definitions ------- */
#define STK_LOG_NORMAL 1      /*!< Logging Level NORMAL */
#define STK_LOG_WARNING 2     /*!< Logging Level WARNING */
#define STK_LOG_ERROR 3       /*!< Logging Level ERROR */
#define STK_LOG_NET_ERROR 4   /*!< Logging Level Network ERROR */

/* ------- Option processing definitions ------- */

/**
 * Generic key value pair structure used to pass options to various STK components.
 * It is common practice to use an array of these on the stack or as a global.
 * STK components do not hold references to these and will copy as needed.
 * It is recommended to use typedef stk_options_t rather than this structure
 */
typedef struct stk_options_stct {
	char *name; /*!< Name of the option */
	void *data; /*!< Pointer to the option - typically a string representation */
} stk_options_t;

/* ------- Per platform typedefs ------- */
/** \typedef stk_uint64
 * 64 bit unsigned int
 */
#ifdef __CYGWIN__
typedef unsigned long long stk_uint64;
#else
typedef unsigned long stk_uint64;
#endif
/** \typedef stk_uint32
 * 32 bit unsigned int
 */
typedef unsigned int stk_uint32;
/** \typedef stk_uint16
 * 16 bit unsigned int
 */
typedef unsigned short stk_uint16;
/** \typedef stk_uint8
 * 8 bit unsigned int
 */
typedef unsigned char stk_uint8;

/** \typedef stk_bool
 * Boolean
 * \see STK_FALSE STK_TRUE
 */
typedef unsigned short stk_bool;

#define STK_FALSE 0 /*!< Boolean value False */
#define STK_TRUE 1  /*!< Boolean value True */

#endif
