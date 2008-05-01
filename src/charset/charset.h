/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef parserutils_charset_charset_h_
#define parserutils_charset_charset_h_

#include <parserutils/errors.h>
#include <parserutils/functypes.h>
#include <parserutils/types.h>

/* Initialise the Charset library for use */
parserutils_error parserutils_charset_initialise(const char *aliases_file,
		parserutils_alloc alloc, void *pw);

/* Clean up after Charset */
parserutils_error parserutils_charset_finalise(parserutils_alloc alloc, 
		void *pw);

#endif

