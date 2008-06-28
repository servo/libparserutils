/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <string.h>

#include "charset/aliases.h"
#include "charset/codecs/codec_impl.h"

#ifdef WITH_ICONV_CODEC
extern parserutils_charset_handler iconv_codec_handler;
#endif

extern parserutils_charset_handler charset_utf8_codec_handler;
extern parserutils_charset_handler charset_utf16_codec_handler;

static parserutils_charset_handler *handler_table[] = {
	&charset_utf8_codec_handler,
	&charset_utf16_codec_handler,
#ifdef WITH_ICONV_CODEC
	&iconv_codec_handler,
#endif
	NULL,
};

/**
 * Create a charset codec
 *
 * \param charset  Target charset
 * \param alloc    Memory (de)allocation function
 * \param pw       Pointer to client-specific private data (may be NULL)
 * \return Pointer to codec instance, or NULL on failure
 */
parserutils_charset_codec *parserutils_charset_codec_create(const char *charset,
		parserutils_alloc alloc, void *pw)
{
	parserutils_charset_codec *codec;
	parserutils_charset_handler **handler;
	const parserutils_charset_aliases_canon * canon;

	if (charset == NULL || alloc == NULL)
		return NULL;

	/* Canonicalise parserutils_charset name. */
	canon = parserutils_charset_alias_canonicalise(charset, 
			strlen(charset));
	if (canon == NULL)
		return NULL;

	/* Search for handler class */
	for (handler = handler_table; *handler != NULL; handler++) {
		if ((*handler)->handles_charset(canon->name))
			break;
	}

	/* None found */
	if ((*handler) == NULL)
		return NULL;

	/* Instantiate class */
	codec = (*handler)->create(canon->name, alloc, pw);
	if (codec == NULL)
		return NULL;

	/* and initialise it */
	codec->mibenum = canon->mib_enum;

	codec->errormode = PARSERUTILS_CHARSET_CODEC_ERROR_LOOSE;

	codec->alloc = alloc;
	codec->alloc_pw = pw;

	return codec;
}

/**
 * Destroy a charset codec
 *
 * \param codec  The codec to destroy
 */
void parserutils_charset_codec_destroy(parserutils_charset_codec *codec)
{
	if (codec == NULL)
		return;

	codec->handler.destroy(codec);

	codec->alloc(codec, 0, codec->alloc_pw);
}

/**
 * Configure a charset codec
 *
 * \param codec   The codec to configure
 * \parem type    The codec option type to configure
 * \param params  Option-specific parameters
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_charset_codec_setopt(
		parserutils_charset_codec *codec,
		parserutils_charset_codec_opttype type,
		parserutils_charset_codec_optparams *params)
{
	if (codec == NULL || params == NULL)
		return PARSERUTILS_BADPARM;

	switch (type) {
	case PARSERUTILS_CHARSET_CODEC_ERROR_MODE:
		codec->errormode = params->error_mode.mode;
		break;
	}

	return PARSERUTILS_OK;
}

/**
 * Encode a chunk of UCS-4 data into a codec's charset
 *
 * \param codec      The codec to use
 * \param source     Pointer to pointer to source data
 * \param sourcelen  Pointer to length (in bytes) of source data
 * \param dest       Pointer to pointer to output buffer
 * \param destlen    Pointer to length (in bytes) of output buffer
 * \return PARSERUTILS_OK on success, appropriate error otherwise.
 *
 * source, sourcelen, dest and destlen will be updated appropriately on exit
 */
parserutils_error parserutils_charset_codec_encode(
		parserutils_charset_codec *codec,
		const uint8_t **source, size_t *sourcelen,
		uint8_t **dest, size_t *destlen)
{
	if (codec == NULL || source == NULL || *source == NULL ||
			sourcelen == NULL || dest == NULL || *dest == NULL ||
			destlen == NULL)
		return PARSERUTILS_BADPARM;

	return codec->handler.encode(codec, source, sourcelen, dest, destlen);
}

/**
 * Decode a chunk of data in a codec's charset into UCS-4
 *
 * \param codec      The codec to use
 * \param source     Pointer to pointer to source data
 * \param sourcelen  Pointer to length (in bytes) of source data
 * \param dest       Pointer to pointer to output buffer
 * \param destlen    Pointer to length (in bytes) of output buffer
 * \return PARSERUTILS_OK on success, appropriate error otherwise.
 *
 * source, sourcelen, dest and destlen will be updated appropriately on exit
 *
 * Call this with a source length of 0 to flush any buffers.
 */
parserutils_error parserutils_charset_codec_decode(
		parserutils_charset_codec *codec,
		const uint8_t **source, size_t *sourcelen,
		uint8_t **dest, size_t *destlen)
{
	if (codec == NULL || source == NULL || *source == NULL ||
			sourcelen == NULL || dest == NULL || *dest == NULL ||
			destlen == NULL)
		return PARSERUTILS_BADPARM;

	return codec->handler.decode(codec, source, sourcelen, dest, destlen);
}

/**
 * Clear a charset codec's encoding state
 *
 * \param codec  The codec to reset
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_charset_codec_reset(
		parserutils_charset_codec *codec)
{
	if (codec == NULL)
		return PARSERUTILS_BADPARM;

	return codec->handler.reset(codec);
}

