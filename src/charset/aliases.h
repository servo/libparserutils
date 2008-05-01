/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef parserutils_charset_aliases_h_
#define parserutils_charset_aliases_h_

#include <inttypes.h>

#include <parserutils/charset/mibenum.h>

typedef struct parserutils_charset_aliases_canon {
	struct parserutils_charset_aliases_canon *next;
	uint16_t mib_enum;
	uint16_t name_len;
	char name[1];
} parserutils_charset_aliases_canon;

/* Load encoding aliases from file */
parserutils_error parserutils_charset_aliases_create(const char *filename,
		parserutils_alloc alloc, void *pw);
/* Destroy encoding aliases */
void parserutils_charset_aliases_destroy(parserutils_alloc alloc, void *pw);

/* Canonicalise an alias name */
parserutils_charset_aliases_canon *parserutils_charset_alias_canonicalise(
		const char *alias, size_t len);

#ifndef NDEBUG
void parserutils_charset_aliases_dump(void);
#endif

#endif
