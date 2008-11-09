/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef parserutils_utils_dict_h_
#define parserutils_utils_dict_h_

#include <parserutils/errors.h>
#include <parserutils/functypes.h>

/**
 * A dictionary entry
 */
struct parserutils_dict_entry
{
	size_t len;			/**< Length of data, in bytes */
	uint8_t *data;			/**< Entry data */
};
typedef struct parserutils_dict_entry parserutils_dict_entry;

struct parserutils_dict;
typedef struct parserutils_dict parserutils_dict;

parserutils_error parserutils_dict_create(parserutils_alloc alloc, void *pw,
		parserutils_dict **dict);
parserutils_error parserutils_dict_destroy(parserutils_dict *dict);

parserutils_error parserutils_dict_insert(parserutils_dict *dict,
		const uint8_t *data, size_t len, 
		const parserutils_dict_entry **result);

#endif

