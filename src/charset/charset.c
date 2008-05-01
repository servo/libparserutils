/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include "charset/aliases.h"
#include "charset/charset.h"

/**
 * Initialise the Charset library for use.
 *
 * This _must_ be called before using any libparserutils charset functions
 *
 * \param aliases_file  Pointer to name of file containing encoding alias data
 * \param alloc         Pointer to (de)allocation function
 * \param pw            Pointer to client-specific private data (may be NULL)
 * \return PARSERUTILS_OK on success, applicable error otherwise.
 */
parserutils_error parserutils_charset_initialise(const char *aliases_file,
		parserutils_alloc alloc, void *pw)
{
	parserutils_error error;

	if (aliases_file == NULL || alloc == NULL)
		return PARSERUTILS_BADPARM;

	error = parserutils_charset_aliases_create(aliases_file, alloc, pw);
	if (error != PARSERUTILS_OK)
		return error;

	return PARSERUTILS_OK;
}

/**
 * Clean up after Libparserutils
 *
 * \param alloc  Pointer to (de)allocation function
 * \param pw     Pointer to client-specific private data (may be NULL)
 * \return PARSERUTILS_OK on success, applicable error otherwise.
 */
parserutils_error parserutils_charset_finalise(parserutils_alloc alloc, 
		void *pw)
{
	if (alloc == NULL)
		return PARSERUTILS_BADPARM;

	parserutils_charset_aliases_destroy(alloc, pw);

	return PARSERUTILS_OK;
}


