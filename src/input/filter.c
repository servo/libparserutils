/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef WITH_ICONV_FILTER
#include <iconv.h>
#endif

#include <parserutils/charset/mibenum.h>
#include <parserutils/charset/codec.h>

#include "input/filter.h"
#include "utils/utils.h"

/** Input filter */
struct parserutils_filter {
#ifdef WITH_ICONV_FILTER
	iconv_t cd;			/**< Iconv conversion descriptor */
	uint16_t int_enc;		/**< The internal encoding */
#else
	parserutils_charset_codec *read_codec;	/**< Read codec */
	parserutils_charset_codec *write_codec;	/**< Write codec */

	uint32_t pivot_buf[64];		/**< Conversion pivot buffer */

	bool leftover;			/**< Data remains from last call */
	uint8_t *pivot_left;		/**< Remaining pivot to write */
	size_t pivot_len;		/**< Length of pivot remaining */
#endif

	struct {
		uint16_t encoding;	/**< Input encoding */
	} settings;			/**< Filter settings */

	parserutils_alloc alloc;	/**< Memory (de)allocation function */
	void *pw;			/**< Client private data */
};

static parserutils_error filter_set_defaults(parserutils_filter *input);
static parserutils_error filter_set_encoding(parserutils_filter *input,
		const char *enc);

/**
 * Create an input filter
 *
 * \param int_enc  Desired encoding of document
 * \param alloc    Function used to (de)allocate data
 * \param pw       Pointer to client-specific private data (may be NULL)
 * \return Pointer to filter instance, or NULL on failure
 */
parserutils_filter *parserutils_filter_create(const char *int_enc,
		parserutils_alloc alloc, void *pw)
{
	parserutils_filter *filter;

	if (int_enc == NULL || alloc == NULL)
		return NULL;

	filter = alloc(NULL, sizeof(*filter), pw);
	if (!filter)
		return NULL;

#ifdef WITH_ICONV_FILTER
	filter->cd = (iconv_t) -1;
	filter->int_enc = parserutils_charset_mibenum_from_name(
			int_enc, strlen(int_enc));
	if (filter->int_enc == 0) {
		alloc(filter, 0, pw);
		return NULL;
	}
#else
	filter->leftover = false;
	filter->pivot_left = NULL;
	filter->pivot_len = 0;
#endif

	filter->alloc = alloc;
	filter->pw = pw;

	if (filter_set_defaults(filter) != PARSERUTILS_OK) {
		filter->alloc(filter, 0, pw);
		return NULL;
	}

#ifndef WITH_ICONV_FILTER
	filter->write_codec = 
			parserutils_charset_codec_create(int_enc, alloc, pw);
	if (filter->write_codec == NULL) {
		if (filter->read_codec != NULL)
			parserutils_charset_codec_destroy(filter->read_codec);
		filter->alloc(filter, 0, pw);
		return NULL;
	}
#endif

	return filter;
}

/**
 * Destroy an input filter
 *
 * \param input  Pointer to filter instance
 */
void parserutils_filter_destroy(parserutils_filter *input)
{
	if (input == NULL)
		return;

#ifdef WITH_ICONV_FILTER
	if (input->cd != (iconv_t) -1)
		iconv_close(input->cd);
#else
	if (input->read_codec != NULL)
		parserutils_charset_codec_destroy(input->read_codec);

	if (input->write_codec != NULL)
		parserutils_charset_codec_destroy(input->write_codec);
#endif

	input->alloc(input, 0, input->pw);

	return;
}

/**
 * Configure an input filter
 *
 * \param input   Pointer to filter instance
 * \param type    Input option type to configure
 * \param params  Option-specific parameters
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_filter_setopt(parserutils_filter *input,
		parserutils_filter_opttype type,
		parserutils_filter_optparams *params)
{
	parserutils_error error = PARSERUTILS_OK;

	if (input == NULL || params == NULL)
		return PARSERUTILS_BADPARM;

	switch (type) {
	case PARSERUTILS_FILTER_SET_ENCODING:
		error = filter_set_encoding(input, params->encoding.name);
		break;
	}

	return error;
}

/**
 * Process a chunk of data
 *
 * \param input   Pointer to filter instance
 * \param data    Pointer to pointer to input buffer
 * \param len     Pointer to length of input buffer
 * \param output  Pointer to pointer to output buffer
 * \param outlen  Pointer to length of output buffer
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 *
 * Call this with an input buffer length of 0 to flush any buffers.
 */
parserutils_error parserutils_filter_process_chunk(parserutils_filter *input,
		const uint8_t **data, size_t *len,
		uint8_t **output, size_t *outlen)
{
	if (input == NULL || data == NULL || *data == NULL || len == NULL ||
			output == NULL || *output == NULL || outlen == NULL)
		return PARSERUTILS_BADPARM;

#ifdef WITH_ICONV_FILTER
	if (iconv(input->cd, (char **) data, len, 
			(char **) output, outlen) == (size_t) -1) {
		switch (errno) {
		case E2BIG:
			return PARSERUTILS_NOMEM;
		case EILSEQ:
			if (*outlen < 3)
				return PARSERUTILS_NOMEM;

			(*output)[0] = 0xef;
			(*output)[1] = 0xbf;
			(*output)[2] = 0xbd;

			*output += 3;
			*outlen -= 3;

			(*data)++;
			(*len)--;

			while (*len > 0) {
				size_t ret;
				
				ret = iconv(input->cd, (char **) data, len, 
						(char **) output, outlen);
				if (ret != (size_t) -1 || errno != EILSEQ)
					break;

				if (*outlen < 3)
					return PARSERUTILS_NOMEM;

				(*output)[0] = 0xef;
				(*output)[1] = 0xbf;
				(*output)[2] = 0xbd;

				*output += 3;
				*outlen -= 3;

				(*data)++;
				(*len)--;
			}

			return errno == E2BIG ? PARSERUTILS_NOMEM 
					      : PARSERUTILS_OK;
		}
	}

	return PARSERUTILS_OK;
#else
	parserutils_error read_error, write_error;

	if (input->leftover) {
		/* Some data left to be written from last call */

		/* Attempt to flush the remaining data. */
		write_error = parserutils_charset_codec_encode(
				input->write_codec,
				(const uint8_t **) &input->pivot_left,
				&input->pivot_len,
				output, outlen);

		if (write_error != PARSERUTILS_OK)
			return write_error;


		/* And clear leftover */
		input->pivot_left = NULL;
		input->pivot_len = 0;
		input->leftover = false;
	}

	while (*len > 0) {
		size_t pivot_len = sizeof(input->pivot_buf);
		uint8_t *pivot = (uint8_t *) input->pivot_buf;

		read_error = parserutils_charset_codec_decode(input->read_codec,
				data, len,
				(uint8_t **) &pivot, &pivot_len);

		pivot = (uint8_t *) input->pivot_buf;
		pivot_len = sizeof(input->pivot_buf) - pivot_len;

		if (pivot_len > 0) {
			write_error = parserutils_charset_codec_encode(
					input->write_codec,
					(const uint8_t **) &pivot,
					&pivot_len,
					output, outlen);

			if (write_error != PARSERUTILS_OK) {
				input->leftover = true;
				input->pivot_left = pivot;
				input->pivot_len = pivot_len;

				return write_error;
			}
		}

		if (read_error != PARSERUTILS_OK && 
				read_error != PARSERUTILS_NOMEM)
			return read_error;
	}

	return PARSERUTILS_OK;
#endif
}

/**
 * Reset an input filter's state
 *
 * \param input  The input filter to reset
 * \param PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_filter_reset(parserutils_filter *input)
{
	if (input == NULL)
		return PARSERUTILS_BADPARM;

#ifdef WITH_ICONV_FILTER
	iconv(input->cd, NULL, 0, NULL, 0);
#else
	parserutils_error error;

	/* Clear pivot buffer leftovers */
	input->pivot_left = NULL;
	input->pivot_len = 0;
	input->leftover = false;

	/* Reset read codec */
	error = parserutils_charset_codec_reset(input->read_codec);
	if (error != PARSERUTILS_OK)
		return error;

	/* Reset write codec */
	error = parserutils_charset_codec_reset(input->write_codec);
	if (error != PARSERUTILS_OK)
		return error;
#endif

	return PARSERUTILS_OK;
}

/**
 * Set an input filter's default settings
 *
 * \param input  Input filter to configure
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error filter_set_defaults(parserutils_filter *input)
{
	parserutils_error error;

	if (input == NULL)
		return PARSERUTILS_BADPARM;

#ifndef WITH_ICONV_FILTER
	input->read_codec = NULL;
	input->write_codec = NULL;
#endif

	input->settings.encoding = 0;
	error = filter_set_encoding(input, "UTF-8");
	if (error != PARSERUTILS_OK)
		return error;

	return PARSERUTILS_OK;
}

/**
 * Set an input filter's encoding
 *
 * \param input  Input filter to configure
 * \param enc    Encoding name
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error filter_set_encoding(parserutils_filter *input,
		const char *enc)
{
	const char *old_enc;
	uint16_t mibenum;

	if (input == NULL || enc == NULL)
		return PARSERUTILS_BADPARM;

	mibenum = parserutils_charset_mibenum_from_name(enc, strlen(enc));
	if (mibenum == 0)
		return PARSERUTILS_INVALID;

	/* Exit early if we're already using this encoding */
	if (input->settings.encoding == mibenum)
		return PARSERUTILS_OK;

	old_enc = parserutils_charset_mibenum_to_name(input->settings.encoding);
	if (old_enc == NULL)
		old_enc = "UTF-8";

#ifdef WITH_ICONV_FILTER
	if (input->cd != (iconv_t) -1)
		iconv_close(input->cd);

	input->cd = iconv_open(
		parserutils_charset_mibenum_to_name(input->int_enc), enc);
#else
	if (input->read_codec != NULL)
		parserutils_charset_codec_destroy(input->read_codec);

	input->read_codec = parserutils_charset_codec_create(enc, input->alloc,
			input->pw);
	if (input->read_codec == NULL)
		return PARSERUTILS_NOMEM;
#endif

	input->settings.encoding = mibenum;

	return PARSERUTILS_OK;
}
