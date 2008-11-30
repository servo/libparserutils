/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef parserutils_utils_chunkarray_h_
#define parserutils_utils_chunkarray_h_

#include <parserutils/errors.h>
#include <parserutils/functypes.h>

struct parserutils_chunkarray;
typedef struct parserutils_chunkarray parserutils_chunkarray;

parserutils_error parserutils_chunkarray_create(parserutils_alloc alloc, 
		void *pw, parserutils_chunkarray **array);
parserutils_error parserutils_chunkarray_destroy(parserutils_chunkarray *array);

parserutils_error parserutils_chunkarray_insert(parserutils_chunkarray *array,
		const void *data, size_t len,
		const void **inserted);

#endif

