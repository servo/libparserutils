/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef parserutils_input_inputstream_h_
#define parserutils_input_inputstream_h_

#include <stdbool.h>
#ifndef NDEBUG
#include <stdio.h>
#endif
#include <stdlib.h>
#include <inttypes.h>

#include <parserutils/errors.h>
#include <parserutils/functypes.h>
#include <parserutils/types.h>
#include <parserutils/charset/utf8.h>
#include <parserutils/utils/buffer.h>

/**
 * Type of charset detection function
 */
typedef parserutils_error (*parserutils_charset_detect_func)(
		const uint8_t *data, size_t len, 
		uint16_t *mibenum, uint32_t *source);

/**
 * Input stream object
 */
typedef struct parserutils_inputstream 
{
	parserutils_buffer *utf8;	/**< Buffer containing UTF-8 data */

	uint32_t cursor;		/**< Byte offset of current position */

	bool had_eof;			/**< Whether EOF has been reached */
} parserutils_inputstream;

/* EOF pseudo-character */
#define PARSERUTILS_INPUTSTREAM_EOF (0xFFFFFFFFU)
/* Out-of-data indicator */
#define PARSERUTILS_INPUTSTREAM_OOD (0xFFFFFFFEU)

/* Create an input stream */
parserutils_inputstream *parserutils_inputstream_create(const char *enc,
		uint32_t encsrc, parserutils_charset_detect_func csdetect,
		parserutils_alloc alloc, void *pw);
/* Destroy an input stream */
void parserutils_inputstream_destroy(parserutils_inputstream *stream);

/* Append data to an input stream */
parserutils_error parserutils_inputstream_append(
		parserutils_inputstream *stream,
		const uint8_t *data, size_t len);
/* Insert data into stream at current location */
parserutils_error parserutils_inputstream_insert(
		parserutils_inputstream *stream,
		const uint8_t *data, size_t len);

/* Slow form of css_inputstream_peek. */
uintptr_t parserutils_inputstream_peek_slow(parserutils_inputstream *stream, 
		size_t offset, size_t *length);

/* Look at the character in the stream that starts at 
 * offset bytes from the cursor
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
static inline uintptr_t parserutils_inputstream_peek(
		parserutils_inputstream *stream, size_t offset, size_t *length)
{
	parserutils_error error = PARSERUTILS_OK;
	size_t len;

	if (stream == NULL)
		return PARSERUTILS_INPUTSTREAM_OOD;

#ifndef NDEBUG
	fprintf(stdout, "Peek: len: %lu cur: %lu off: %lu\n",
			stream->utf8->length, stream->cursor, offset);
	parserutils_buffer_randomise(stream->utf8);
#endif

#define IS_ASCII(x) (((x) & 0x80) == 0)

	if (stream->cursor + offset < stream->utf8->length) {
		if (IS_ASCII(stream->utf8->data[stream->cursor + offset])) {
			len = 1;
		} else {
			error = parserutils_charset_utf8_char_byte_length(
				stream->utf8->data + stream->cursor + offset,
				&len);

			if (error != PARSERUTILS_OK && 
					error != PARSERUTILS_NEEDDATA)
				return PARSERUTILS_INPUTSTREAM_OOD;
		}
	}

#undef IS_ASCII

	if (stream->cursor + offset == stream->utf8->length ||
			error == PARSERUTILS_NEEDDATA) {
		uintptr_t data = parserutils_inputstream_peek_slow(stream, 
				offset, length);
#ifndef NDEBUG
		fprintf(stdout, "clen: %lu\n", *length);
#endif
		return data;
	}

#ifndef NDEBUG
	fprintf(stdout, "clen: %lu\n", len);
#endif

	*length = len;

	return (uintptr_t) (stream->utf8->data + stream->cursor + offset);
}

/**
 * Advance the stream's current position
 *
 * \param stream  The stream whose position to advance
 * \param bytes   The number of bytes to advance
 */
static inline void parserutils_inputstream_advance(
		parserutils_inputstream *stream, size_t bytes)
{
	if (stream == NULL)
		return;

#ifndef NDEBUG
	fprintf(stdout, "Advance: len: %lu cur: %lu bytes: %lu\n",
			stream->utf8->length, stream->cursor, bytes);
#endif

	if (bytes > stream->utf8->length - stream->cursor)
		abort();

	if (stream->cursor == stream->utf8->length)
		return;

	stream->cursor += bytes;
}

/* Read the document charset */
const char *parserutils_inputstream_read_charset(
		parserutils_inputstream *stream, uint32_t *source);

#endif

