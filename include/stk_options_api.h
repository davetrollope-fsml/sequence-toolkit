/** @file stk_options_api.h
 * The Option module in the Sequence Toolkit provides APIs to interrogate and manage
 * various options throughout the STK system. Most modules comprise of the ability
 * to be configured through this Option API.
 */
#ifndef STK_OPTION_API_H
#define STK_OPTION_API_H
#include "stk_common.h"

/**
 * Find a named option in an array of options
 * \param options NULL terminated array of options to be searched
 * \param name Option name being searched for
 * \param last NULL for a new search, or a pointer address to store last iteration key
 * \returns the data value for the named option (or NULL if not present)
 */
void *stk_find_option(stk_options_t *options, char *name, stk_options_t **last);

/**
 * Copy an options array and extend it (alloc new) to make room for new options.
 * \param options NULL terminated array of options to be searched. (Passing a NULL pointer allocs new)
 * \param number_to_extend Number of additional options to be added to the array
 * \returns A newly allocated options array
 */
stk_options_t *stk_copy_extend_options(stk_options_t *options, int number_to_extend);

/**
 * Append a key value pair to an options array.
 * Note: This function does not extend/realloc the memory - use stk_copy_extend_options()
 * to make space if not allocated large enough then call this.
 * \param options NULL terminated array of options to be searched
 * \param name Name of key/value pair to be added
 * \param data Data of key/value pair to be added
 * \returns STK_SUCCESS if the array was appended
 * \see stk_copy_extend_options()
 */
stk_ret stk_append_option(stk_options_t *options, char *name, void *data);

/**
 * Update an existing key value pair in an options array.
 * \param options NULL terminated array of options to be searched
 * \param name Name of key/value pair to be updated
 * \param data New data of key/value pair
 * \param old_data Pointer to store old data value of key/value pair, maybe NULL
 * \returns STK_SUCCESS if the array was updated, or !STK_SUCCESS if the option was not found
 */
stk_ret stk_update_option(stk_options_t *options, char *name, void *data, void **old_data);

/**
 * Free a dynamically allocated options array.
 * \param options The options array to be freed
 * \returns Whether the array was freed
 * \see stk_copy_extend_options()
 */
stk_ret stk_free_options(stk_options_t *options);

/**
 * API to build an options table from a string in the following format.
 * <key> <value>
 * <subscope>
 *  <key> <value>
 * Whitespace indentation is significant
 * \returns An allocated options table
 * \see stk_free_built_options()
 */
stk_options_t *stk_build_options(char *str,int sz);

/**
 * Free options created with stk_build_options().
 * This API does a shallow free of each name/value element
 * Applications must still call stk_free_options().
 * It cannot recurse in to scopes/sub-options because there is no data
 * indicating the type of value.
 * Applications must call stk_find_options() and call this on every sub-option.
 * \see stk_find_option()
 */
stk_ret stk_free_built_options(stk_options_t *options);
#endif
