/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef parserutils_utils_hash_h_
#define parserutils_utils_hash_h_

#include <parserutils/errors.h>
#include <parserutils/functypes.h>

typedef struct parserutils_hash_entry {
	size_t len;
	const uint8_t *data;
} parserutils_hash_entry;

struct parserutils_hash;
typedef struct parserutils_hash parserutils_hash;

parserutils_error parserutils_hash_create(parserutils_alloc alloc, void *pw,
		parserutils_hash **hash);
parserutils_error parserutils_hash_destroy(parserutils_hash *hash);

parserutils_error parserutils_hash_insert(parserutils_hash *hash,
		const uint8_t *data, size_t len,
		const parserutils_hash_entry **inserted);

#endif

