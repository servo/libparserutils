/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <parserutils/charset/mibenum.h>
#include <parserutils/charset/utf8.h>
#include <parserutils/input/inputstream.h>

#include "input/filter.h"
#include "utils/utils.h"

/**
 * Private input stream definition
 */
typedef struct parserutils_inputstream_private {
	parserutils_inputstream public;	/**< Public part. Must be first */

	parserutils_buffer *raw;	/**< Buffer containing raw data */

	bool done_first_chunk;		/**< Whether the first chunk has 
					 * been processed */

	uint16_t mibenum;		/**< MIB enum for charset, or 0 */
	uint32_t encsrc;		/**< Charset source */

	parserutils_filter *input;	/**< Charset conversion filter */

	parserutils_charset_detect_func csdetect; /**< Charset detection func.*/

	parserutils_alloc alloc;	/**< Memory (de)allocation function */
	void *pw;			/**< Client private data */
} parserutils_inputstream_private;

static inline parserutils_error parserutils_inputstream_refill_buffer(
		parserutils_inputstream_private *stream);
static inline parserutils_error parserutils_inputstream_strip_bom(
		uint16_t mibenum, parserutils_buffer *buffer);

/**
 * Create an input stream
 *
 * \param enc       Document charset, or NULL to autodetect
 * \param encsrc    Value for encoding source, if specified, or 0
 * \param csdetect  Charset detection function, or NULL
 * \param alloc     Memory (de)allocation function
 * \param pw        Pointer to client-specific private data (may be NULL)
 * \return Pointer to stream instance, or NULL on failure
 *
 * The value 0 is defined as being the lowest priority encoding source 
 * (i.e. the default fallback encoding). Beyond this, no further 
 * interpretation is made upon the encoding source.
 */
parserutils_inputstream *parserutils_inputstream_create(const char *enc,
		uint32_t encsrc, parserutils_charset_detect_func csdetect,
		parserutils_alloc alloc, void *pw)
{
	parserutils_inputstream_private *stream;

	if (alloc == NULL)
		return NULL;

	stream = alloc(NULL, sizeof(parserutils_inputstream_private), pw);
	if (stream == NULL)
		return NULL;

	stream->raw = parserutils_buffer_create(alloc, pw);
	if (stream->raw == NULL) {
		alloc(stream, 0, pw);
		return NULL;
	}

	stream->public.utf8 = parserutils_buffer_create(alloc, pw);
	if (stream->public.utf8 == NULL) {
		parserutils_buffer_destroy(stream->raw);
		alloc(stream, 0, pw);
		return NULL;
	}

	stream->public.cursor = 0;
	stream->public.had_eof = false;
	stream->done_first_chunk = false;

	stream->input = parserutils_filter_create("UTF-8", alloc, pw);
	if (stream->input == NULL) {
		parserutils_buffer_destroy(stream->public.utf8);
		parserutils_buffer_destroy(stream->raw);
		alloc(stream, 0, pw);
		return NULL;
	}

	if (enc != NULL) {
		parserutils_error error;
		parserutils_filter_optparams params;

		stream->mibenum = 
			parserutils_charset_mibenum_from_name(enc, strlen(enc));

		if (stream->mibenum != 0) {
			params.encoding.name = enc;

			error = parserutils_filter_setopt(stream->input,
					PARSERUTILS_FILTER_SET_ENCODING, 
					&params);
			if (error != PARSERUTILS_OK && 
					error != PARSERUTILS_INVALID) {
				parserutils_filter_destroy(stream->input);
				parserutils_buffer_destroy(stream->public.utf8);
				parserutils_buffer_destroy(stream->raw);
				alloc(stream, 0, pw);
				return NULL;
			}

			stream->encsrc = encsrc;
		}
	} else {
		stream->mibenum = 0;
		stream->encsrc = 0;
	}

	stream->csdetect = csdetect;

	stream->alloc = alloc;
	stream->pw = pw;

	return (parserutils_inputstream *) stream;
}

/**
 * Destroy an input stream
 *
 * \param stream  Input stream to destroy
 */
void parserutils_inputstream_destroy(parserutils_inputstream *stream)
{
	parserutils_inputstream_private *s = 
			(parserutils_inputstream_private *) stream;

	if (stream == NULL)
		return;

	parserutils_filter_destroy(s->input);
	parserutils_buffer_destroy(s->public.utf8);
	parserutils_buffer_destroy(s->raw);
	s->alloc(s, 0, s->pw);
}

/**
 * Append data to an input stream
 *
 * \param stream  Input stream to append data to
 * \param data    Data to append (in document charset), or NULL to flag EOF
 * \param len     Length, in bytes, of data
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_inputstream_append(
		parserutils_inputstream *stream, 
		const uint8_t *data, size_t len)
{
	parserutils_inputstream_private *s = 
			(parserutils_inputstream_private *) stream;

	if (stream == NULL)
		return PARSERUTILS_BADPARM;

	if (data == NULL) {
		s->public.had_eof = true;
		return PARSERUTILS_OK;
	}

	return parserutils_buffer_append(s->raw, data, len);
}

/**
 * Insert data into stream at current location
 *
 * \param stream  Input stream to insert into
 * \param data    Data to insert (UTF-8 encoded)
 * \param len     Length, in bytes, of data
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_inputstream_insert(
		parserutils_inputstream *stream,
		const uint8_t *data, size_t len)
{
	parserutils_inputstream_private *s = 
			(parserutils_inputstream_private *) stream;

	if (stream == NULL || data == NULL)
		return PARSERUTILS_BADPARM;

	return parserutils_buffer_insert(s->public.utf8, s->public.cursor, 
			data, len);
}

#define IS_ASCII(x) (((x) & 0x80) == 0)

/* Look at the character in the stream that starts at 
 * offset bytes from the cursor (slow version)
 *
 * \param stream  Stream to look in
 * \param offset  Byte offset of start of character
 * \param length  Pointer to location to receive character length (in bytes)
 * \return Pointer to character data, or EOF or OOD.
 *
 * Once the character pointed to by the result of this call has been advanced
 * past (i.e. parserutils_inputstream_advance has caused the stream cursor to 
 * pass over the character), then no guarantee is made as to the validity of 
 * the data pointed to. Thus, any attempt to dereference the pointer after 
 * advancing past the data it points to is a bug.
 */
uintptr_t parserutils_inputstream_peek_slow(parserutils_inputstream *stream, 
		size_t offset, size_t *length)
{
	parserutils_inputstream_private *s = 
			(parserutils_inputstream_private *) stream;
	parserutils_error error = PARSERUTILS_OK;
	size_t len;

	if (stream == NULL)
		return PARSERUTILS_INPUTSTREAM_OOD;

	/* There's insufficient data in the buffer, so read some more */
	if (s->raw->length == 0) {
		/* No more data to be had */
		return s->public.had_eof ? PARSERUTILS_INPUTSTREAM_EOF
					 : PARSERUTILS_INPUTSTREAM_OOD;
	}

	/* Refill utf8 buffer from raw buffer */
	error = parserutils_inputstream_refill_buffer(s);
	if (error != PARSERUTILS_OK)
		return PARSERUTILS_INPUTSTREAM_OOD;

	/* Now try the read */
	if (IS_ASCII(s->public.utf8->data[s->public.cursor + offset])) {
		len = 1;
	} else {
		error = parserutils_charset_utf8_char_byte_length(
			s->public.utf8->data + s->public.cursor + offset,
			&len);

		if (error != PARSERUTILS_OK && error != PARSERUTILS_NEEDDATA)
			return PARSERUTILS_INPUTSTREAM_OOD;

		if (error == PARSERUTILS_NEEDDATA) {
			return s->public.had_eof ? PARSERUTILS_INPUTSTREAM_EOF
						 : PARSERUTILS_INPUTSTREAM_OOD;
		}
	}

	*length = len;

	return (uintptr_t) (s->public.utf8->data + s->public.cursor + offset);
}

#undef IS_ASCII

/**
 * Read the source charset of the input stream
 *
 * \param stream  Input stream to query
 * \param source  Pointer to location to receive charset source identifier
 * \return Pointer to charset name (constant; do not free)
 */
const char *parserutils_inputstream_read_charset(
		parserutils_inputstream *stream, uint32_t *source)
{
	parserutils_inputstream_private *s = 
			(parserutils_inputstream_private *) stream;

	if (stream == NULL || source == NULL)
		return NULL;

	*source = s->encsrc;

	if (s->encsrc == 0)
		return "UTF-8";

	return parserutils_charset_mibenum_to_name(s->mibenum);
}

/******************************************************************************
 ******************************************************************************/

/**
 * Refill the UTF-8 buffer from the raw buffer
 *
 * \param stream  The inputstream to operate on
 * \return PARSERUTILS_OK on success
 */
parserutils_error parserutils_inputstream_refill_buffer(
		parserutils_inputstream_private *stream)
{
	const uint8_t *raw;
	uint8_t *utf8;
	size_t raw_length, utf8_space;
	parserutils_error error;

	/* If this is the first chunk of data, we must detect the charset and
	 * strip the BOM, if one exists */
	if (!stream->done_first_chunk) {
		if (stream->csdetect != NULL) {
			error = stream->csdetect(stream->raw->data, 
				stream->raw->length,
				&stream->mibenum, &stream->encsrc);
			if (error != PARSERUTILS_OK)
				return error;
		} else {
			/* Default to UTF-8 */
			stream->mibenum = 
				parserutils_charset_mibenum_from_name("UTF-8", 
					SLEN("UTF-8"));
			stream->encsrc = 0;
		}

		if (stream->mibenum == 0)
			abort();

		error = parserutils_inputstream_strip_bom(stream->mibenum, 
				stream->raw);
		if (error != PARSERUTILS_OK)
			return error;

		stream->done_first_chunk = true;
	}

	/* Work out how to perform the buffer fill */
	if (stream->public.cursor == stream->public.utf8->length) {
		/* Cursor's at the end, so simply reuse the entire buffer */
		utf8 = stream->public.utf8->data;
		utf8_space = stream->public.utf8->allocated;
	} else {
		/* Cursor's not at the end, so shift data after cursor to the
		 * bottom of the buffer. If the buffer's still over half full, 
		 * extend it. */
		memmove(stream->public.utf8->data,
			stream->public.utf8->data + stream->public.cursor,
			stream->public.utf8->length - stream->public.cursor);

		stream->public.utf8->length -= stream->public.cursor;

		if (stream->public.utf8->length > 
				stream->public.utf8->allocated / 2) {
			error = parserutils_buffer_grow(stream->public.utf8);
			if (error != PARSERUTILS_OK)
				return error;
		}

		utf8 = stream->public.utf8->data + stream->public.utf8->length;
		utf8_space = stream->public.utf8->allocated - 
				stream->public.utf8->length;
	}

	raw = stream->raw->data;
	raw_length = stream->raw->length;

	/* Try to fill utf8 buffer from the raw data */
	error = parserutils_filter_process_chunk(stream->input, 
			&raw, &raw_length, &utf8, &utf8_space);
	/* _NOMEM implies that there's more input to read than available space
	 * in the utf8 buffer. That's fine, so we'll ignore that error. */
	if (error != PARSERUTILS_OK && error != PARSERUTILS_NOMEM)
		return error;

	/* Remove the raw data we've processed from the raw buffer */
	error = parserutils_buffer_discard(stream->raw, 0, 
			stream->raw->length - raw_length);
	if (error != PARSERUTILS_OK)
		return error;

	/* Fix up the utf8 buffer information */
	stream->public.utf8->length = 
			stream->public.utf8->allocated - utf8_space;

	/* Finally, fix up the cursor */
	stream->public.cursor = 0;

	return PARSERUTILS_OK;
}

/**
 * Strip a BOM from a buffer in the given encoding
 *
 * \param mibenum  The character set of the buffer
 * \param buffer   The buffer to process
 */
parserutils_error parserutils_inputstream_strip_bom(uint16_t mibenum, 
		parserutils_buffer *buffer)
{
	static uint16_t utf8;
	static uint16_t utf16;
	static uint16_t utf16be;
	static uint16_t utf16le;
	static uint16_t utf32;
	static uint16_t utf32be;
	static uint16_t utf32le;

	if (utf8 == 0) {
		utf8 = parserutils_charset_mibenum_from_name("UTF-8", 
				SLEN("UTF-8"));
		utf16 = parserutils_charset_mibenum_from_name("UTF-16", 
				SLEN("UTF-16"));
		utf16be = parserutils_charset_mibenum_from_name("UTF-16BE",
				SLEN("UTF-16BE"));
		utf16le = parserutils_charset_mibenum_from_name("UTF-16LE",
				SLEN("UTF-16LE"));
		utf32 = parserutils_charset_mibenum_from_name("UTF-32", 
				SLEN("UTF-32"));
		utf32be = parserutils_charset_mibenum_from_name("UTF-32BE",
				SLEN("UTF-32BE"));
		utf32le = parserutils_charset_mibenum_from_name("UTF-32LE",
				SLEN("UTF-32LE"));
	}

	/** \todo Handle unmarked UTF-16 and UTF-32. Endianness is specified 
	 * by the BOM, if present, or is assumed to be big endian. */

#define UTF32_BOM_LEN (4)
#define UTF16_BOM_LEN (2)
#define UTF8_BOM_LEN  (3)

	if (mibenum == utf8) {
		if (buffer->length >= UTF8_BOM_LEN && 
				buffer->data[0] == 0xEF &&
				buffer->data[1] == 0xBB && 
				buffer->data[2] == 0xBF) {
			return parserutils_buffer_discard(
					buffer, 0, UTF8_BOM_LEN);
		}
	} else if (mibenum == utf16be) {
		if (buffer->length >= UTF16_BOM_LEN &&
				buffer->data[0] == 0xFE &&
				buffer->data[1] == 0xFF) {
			return parserutils_buffer_discard(
					buffer, 0, UTF16_BOM_LEN);
		}
	} else if (mibenum == utf16le) {
		if (buffer->length >= UTF16_BOM_LEN &&
				buffer->data[0] == 0xFF &&
				buffer->data[1] == 0xFE) {
			return parserutils_buffer_discard(
					buffer, 0, UTF16_BOM_LEN);
		}
	} else if (mibenum == utf32be) {
		if (buffer->length >= UTF32_BOM_LEN &&
				buffer->data[0] == 0x00 &&
				buffer->data[1] == 0x00 &&
				buffer->data[2] == 0xFE &&
				buffer->data[3] == 0xFF) {
			return parserutils_buffer_discard(
					buffer, 0, UTF32_BOM_LEN);
		}
	} else if (mibenum == utf32le) {
		if (buffer->length >= UTF32_BOM_LEN &&
				buffer->data[0] == 0xFF &&
				buffer->data[1] == 0xFE &&
				buffer->data[2] == 0x00 &&
				buffer->data[3] == 0x00) {
			return parserutils_buffer_discard(
					buffer, 0, UTF32_BOM_LEN);
		}
	}

#undef UTF8_BOM_LEN
#undef UTF16_BOM_LEN
#undef UTF32_BOM_LEN

	return PARSERUTILS_OK;
}

