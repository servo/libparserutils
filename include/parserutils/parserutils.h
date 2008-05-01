/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef parserutils_parserutils_h_
#define parserutils_parserutils_h_

#include <parserutils/errors.h>
#include <parserutils/functypes.h>
#include <parserutils/types.h>

/* Initialise the ParserUtils library for use */
parserutils_error parserutils_initialise(const char *aliases_file,
		parserutils_alloc alloc, void *pw);

/* Clean up after ParserUtils */
parserutils_error parserutils_finalise(parserutils_alloc alloc, void *pw);

#endif

