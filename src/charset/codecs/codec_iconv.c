/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 */

/* This codec is hideously slow. Only use it as a last resort */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* We put this here rather than at the top as GCC complains 
 * about the source file being empty otherwise. */
#ifdef WITH_ICONV_CODEC

#include <iconv.h>

/* These two are for htonl / ntohl */
#include <arpa/inet.h>
#include <netinet/in.h>

#include <parserutils/charset/mibenum.h>

#include "charset/codecs/codec_impl.h"
#include "utils/utils.h"

/**
 * Iconv-based charset codec
 */
typedef struct iconv_codec {
	parserutils_charset_codec base;	/**< Base class */

	iconv_t read_cd;		/**< Iconv handle for reading */
#define INVAL_BUFSIZE (32)
	uint8_t inval_buf[INVAL_BUFSIZE];	/**< Buffer for fixing up
						 * incomplete input
						 * sequences */
	size_t inval_len;		/**< Number of bytes in inval_buf */

#define READ_BUFSIZE (8)
	uint32_t read_buf[READ_BUFSIZE];	/**< Buffer for partial
						 * output sequences (decode)
						 */
	size_t read_len;		/**< Number of characters in
					 * read_buf */

	iconv_t write_cd;		/**< Iconv handle for writing */
#define WRITE_BUFSIZE (8)
	uint32_t write_buf[WRITE_BUFSIZE];	/**< Buffer for partial
						 * output sequences (encode)
						 */
	size_t write_len;		/**< Number of characters in
					 * write_buf */
} iconv_codec;


static bool iconv_codec_handles_charset(const char *charset);
static parserutils_charset_codec *iconv_codec_create(const char *charset,
		parserutils_alloc alloc, void *pw);
static void iconv_codec_destroy (parserutils_charset_codec *codec);
static parserutils_error iconv_codec_encode(parserutils_charset_codec *codec,
		const uint8_t **source, size_t *sourcelen,
		uint8_t **dest, size_t *destlen);
static parserutils_error iconv_codec_decode(parserutils_charset_codec *codec,
		const uint8_t **source, size_t *sourcelen,
		uint8_t **dest, size_t *destlen);
static parserutils_error iconv_codec_reset(parserutils_charset_codec *codec);
static parserutils_error iconv_codec_output_decoded_char(
		iconv_codec *c, uint32_t ucs4, uint8_t **dest,
		size_t *destlen);
static parserutils_error iconv_codec_read_char(iconv_codec *c,
		const uint8_t **source, size_t *sourcelen,
		uint8_t **dest, size_t *destlen);
static parserutils_error iconv_codec_write_char(iconv_codec *c,
		uint32_t ucs4, uint8_t **dest, size_t *destlen);

/**
 * Determine whether this codec handles a specific charset
 *
 * \param charset  Charset to test
 * \return true if handleable, false otherwise
 */
bool iconv_codec_handles_charset(const char *charset)
{
	iconv_t cd;
	bool ret;

	cd = iconv_open("UCS-4", charset);

	ret = (cd != (iconv_t) -1);

	if (ret)
		iconv_close(cd);

	return ret;
}

/**
 * Create an iconv-based codec
 *
 * \param charset  The charset to read from / write to
 * \param alloc    Memory (de)allocation function
 * \param pw       Pointer to client-specific private data (may be NULL)
 * \return Pointer to codec, or NULL on failure
 */
parserutils_charset_codec *iconv_codec_create(const char *charset,
		parserutils_alloc alloc, void *pw)
{
	iconv_codec *codec;

	codec = alloc(NULL, sizeof(iconv_codec), pw);
	if (codec == NULL)
		return NULL;

	codec->read_cd = iconv_open("UCS-4", charset);
	if (codec->read_cd == (iconv_t) -1) {
		alloc(codec, 0, pw);
		return NULL;
	}

	codec->write_cd = iconv_open(charset, "UCS-4");
	if (codec->write_cd == (iconv_t) -1) {
		iconv_close(codec->read_cd);
		alloc(codec, 0, pw);
		return NULL;
	}

	codec->inval_buf[0] = '\0';
	codec->inval_len = 0;

	codec->read_buf[0] = 0;
	codec->read_len = 0;

	codec->write_buf[0] = 0;
	codec->write_len = 0;

	/* Finally, populate vtable */
	codec->base.handler.destroy = iconv_codec_destroy;
	codec->base.handler.encode = iconv_codec_encode;
	codec->base.handler.decode = iconv_codec_decode;
	codec->base.handler.reset = iconv_codec_reset;

	return (parserutils_charset_codec *) codec;
}

/**
 * Destroy an iconv-based codec
 *
 * \param codec  The codec to destroy
 */
void iconv_codec_destroy (parserutils_charset_codec *codec)
{
	iconv_codec *c = (iconv_codec *) codec;

	iconv_close(c->read_cd);
	iconv_close(c->write_cd);

	return;
}

/**
 * Encode a chunk of UCS-4 data into an iconv-based codec's charset
 *
 * \param codec      The codec to use
 * \param source     Pointer to pointer to source data
 * \param sourcelen  Pointer to length (in bytes) of source data
 * \param dest       Pointer to pointer to output buffer
 * \param destlen    Pointer to length (in bytes) of output buffer
 * \return PARSERUTILS_OK          on success,
 *         PARSERUTILS_NOMEM       if output buffer is too small,
 *         PARSERUTILS_INVALID     if a character cannot be represented and the
 *                             codec's error handling mode is set to STRICT,
 *
 * On exit, ::source will point immediately _after_ the last input character
 * read. Any remaining output for the character will be buffered by the
 * codec for writing on the next call.
 *
 * Note that, if failure occurs whilst attempting to write any output
 * buffered by the last call, then ::source and ::sourcelen will remain
 * unchanged (as nothing more has been read).
 *
 * ::sourcelen will be reduced appropriately on exit.
 *
 * ::dest will point immediately _after_ the last character written.
 *
 * ::destlen will be reduced appropriately on exit.
 */
parserutils_error iconv_codec_encode(parserutils_charset_codec *codec,
		const uint8_t **source, size_t *sourcelen,
		uint8_t **dest, size_t *destlen)
{
	iconv_codec *c = (iconv_codec *) codec;
	uint32_t ucs4;
	const uint32_t *towrite;
	size_t towritelen;
	parserutils_error error;

	/* Process any outstanding characters from the previous call */
	if (c->write_len > 0) {
		uint32_t *pwrite = c->write_buf;

		while (c->write_len > 0) {
			error = iconv_codec_write_char(c, pwrite[0],
					dest, destlen);
			if (error != PARSERUTILS_OK) {
				/* Copy outstanding chars down, skipping
				 * invalid one, if present, so as to avoid
				 * reprocessing the invalid character */
				if (error == PARSERUTILS_INVALID) {
					for (ucs4 = 1; ucs4 < c->write_len;
							ucs4++) {
						c->write_buf[ucs4] =
								pwrite[ucs4];
					}
				}

				return error;
			}

			pwrite++;
			c->write_len--;
		}
	}

	/* Now process the characters for this call */
	while (*sourcelen > 0) {
		towrite = (const uint32_t *) (const void *) *source;
		towritelen = 1;
		ucs4 = *towrite;

		/* Output current character(s) */
		while (towritelen > 0) {
			error = iconv_codec_write_char(c, towrite[0],
					dest, destlen);

			if (error != PARSERUTILS_OK) {
				ucs4 = (error == PARSERUTILS_INVALID) ? 1 : 0;

				if (towritelen - ucs4 >= WRITE_BUFSIZE)
					abort();

				c->write_len = towritelen - ucs4;

				/* Copy pending chars to save area, for
				 * processing next call; skipping invalid
				 * character, if present, so it's not
				 * reprocessed. */
				for (; ucs4 < towritelen; ucs4++) {
					c->write_buf[ucs4] = towrite[ucs4];
				}

				/* Claim character we've just buffered,
				 * so it's not repreocessed */
				*source += 4;
				*sourcelen -= 4;

				return error;
			}

			towrite++;
			towritelen--;
		}

		*source += 4;
		*sourcelen -= 4;
	}

	return PARSERUTILS_OK;
}

/**
 * Decode a chunk of data in an iconv-based codec's charset into UCS-4
 *
 * \param codec      The codec to use
 * \param source     Pointer to pointer to source data
 * \param sourcelen  Pointer to length (in bytes) of source data
 * \param dest       Pointer to pointer to output buffer
 * \param destlen    Pointer to length (in bytes) of output buffer
 * \return PARSERUTILS_OK          on success,
 *         PARSERUTILS_NOMEM       if output buffer is too small,
 *         PARSERUTILS_INVALID     if a character cannot be represented and the
 *                            codec's error handling mode is set to STRICT,
 *
 * On exit, ::source will point immediately _after_ the last input character
 * read, if the result is _OK or _NOMEM. Any remaining output for the
 * character will be buffered by the codec for writing on the next call.
 *
 * In the case of the result being _INVALID, ::source will point _at_ the 
 * last input character read; nothing will be written or buffered for the 
 * failed character. It is up to the client to fix the cause of the failure 
 * and retry the decoding process.
 *
 * Note that, if failure occurs whilst attempting to write any output
 * buffered by the last call, then ::source and ::sourcelen will remain
 * unchanged (as nothing more has been read).
 *
 * If STRICT error handling is configured and an illegal sequence is split
 * over two calls, then _INVALID will be returned from the second call,
 * but ::source will point mid-way through the invalid sequence (i.e. it
 * will be unmodified over the second call). In addition, the internal
 * incomplete-sequence buffer will be emptied, such that subsequent calls
 * will progress, rather than re-evaluating the same invalid sequence.
 *
 * ::sourcelen will be reduced appropriately on exit.
 *
 * ::dest will point immediately _after_ the last character written.
 *
 * ::destlen will be reduced appropriately on exit.
 *
 * Call this with a source length of 0 to flush the output buffer.
 */
parserutils_error iconv_codec_decode(parserutils_charset_codec *codec,
		const uint8_t **source, size_t *sourcelen,
		uint8_t **dest, size_t *destlen)
{
	iconv_codec *c = (iconv_codec *) codec;
	parserutils_error error;

	if (c->read_len > 0) {
		/* Output left over from last decode
		 * Attempt to finish this here */
		uint32_t *pread = c->read_buf;

		while (c->read_len > 0 && *destlen >= c->read_len * 4) {
			*((uint32_t *) (void *) *dest) = pread[0];

			*dest += 4;
			*destlen -= 4;

			pread++;
			c->read_len--;
		}

		if (*destlen < c->read_len * 4) {
			/* Run out of output buffer */
			size_t i;

			/* Shuffle remaining output down */
			for (i = 0; i < c->read_len; i++) {
				c->read_buf[i] = pread[i];
			}

			return PARSERUTILS_NOMEM;
		}
	}

	if (c->inval_len > 0) {
		/* The last decode ended in an incomplete sequence.
		 * Fill up inval_buf with data from the start of the
		 * new chunk and process it. */
		uint8_t *in = c->inval_buf;
		size_t ol = c->inval_len;
		size_t l = min(INVAL_BUFSIZE - ol - 1, *sourcelen);
		size_t orig_l = l;

		memcpy(c->inval_buf + ol, *source, l);

		l += c->inval_len;

		error = iconv_codec_read_char(c,
				(const uint8_t **) &in, &l, dest, destlen);
		if (error != PARSERUTILS_OK && error != PARSERUTILS_NOMEM) {
			return error;
		}


		/* And now, fix everything up so the normal processing
		 * does the right thing. */
		*source += max((signed) (orig_l - l), 0);
		*sourcelen -= max((signed) (orig_l - l), 0);

		/* Failed to resolve an incomplete character and
		 * ran out of buffer space. No recovery strategy
		 * possible, so explode everywhere. */
		if ((orig_l + ol) - l == 0)
			abort();

		/* Handle memry exhaustion case from above */
		if (error != PARSERUTILS_OK)
			return error;
	}

	while (*sourcelen > 0) {
		error = iconv_codec_read_char(c,
				source, sourcelen, dest, destlen);
		if (error != PARSERUTILS_OK) {
			return error;
		}
	}

	return PARSERUTILS_OK;
}

/**
 * Clear an iconv-based codec's encoding state
 *
 * \param codec  The codec to reset
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error iconv_codec_reset(parserutils_charset_codec *codec)
{
	iconv_codec *c = (iconv_codec *) codec;

	iconv(c->read_cd, NULL, NULL, NULL, NULL);
	iconv(c->write_cd, NULL, NULL, NULL, NULL);

	c->inval_buf[0] = '\0';
	c->inval_len = 0;

	c->read_buf[0] = 0;
	c->read_len = 0;

	c->write_buf[0] = 0;
	c->write_len = 0;

	return PARSERUTILS_OK;
}

/**
 * Output a UCS-4 character
 *
 * \param c        Codec to use
 * \param ucs4     UCS-4 character (big endian)
 * \param dest     Pointer to pointer to output buffer
 * \param destlen  Pointer to output buffer length
 * \return PARSERUTILS_OK          on success,
 *         PARSERUTILS_NOMEM       if output buffer is too small,
 */
parserutils_error iconv_codec_output_decoded_char(iconv_codec *c,
		uint32_t ucs4, uint8_t **dest, size_t *destlen)
{
	if (*destlen < 4) {
		/* Run out of output buffer */

		c->read_len = 1;
		c->read_buf[0] = ucs4;

		return PARSERUTILS_NOMEM;
	}

	*((uint32_t *) (void *) *dest) = ucs4;
	*dest += 4;
	*destlen -= 4;

	return PARSERUTILS_OK;
}

/**
 * Read a character from the codec's native charset to UCS-4 (big endian)
 *
 * \param c          The codec
 * \param source     Pointer to pointer to source buffer (updated on exit)
 * \param sourcelen  Pointer to length of source buffer (updated on exit)
 * \param dest       Pointer to pointer to output buffer (updated on exit)
 * \param destlen    Pointer to length of output buffer (updated on exit)
 * \return PARSERUTILS_OK on success,
 *         PARSERUTILS_NOMEM       if output buffer is too small,
 *         PARSERUTILS_INVALID     if a character cannot be represented and the
 *                            codec's error handling mode is set to STRICT,
 *
 * On exit, ::source will point immediately _after_ the last input character
 * read, if the result is _OK or _NOMEM. Any remaining output for the
 * character will be buffered by the codec for writing on the next call.
 *
 * In the case of the result being _INVALID, ::source will point _at_ the 
 * last input character read; nothing will be written or buffered for the 
 * failed character. It is up to the client to fix the cause of the failure 
 * and retry the decoding process.
 *
 * ::sourcelen will be reduced appropriately on exit.
 *
 * ::dest will point immediately _after_ the last character written.
 *
 * ::destlen will be reduced appropriately on exit.
 */
parserutils_error iconv_codec_read_char(iconv_codec *c,
		const uint8_t **source, size_t *sourcelen,
		uint8_t **dest, size_t *destlen)
{
	size_t iconv_ret;
	const uint8_t *origsrc = *source;
	size_t origsrclen = *sourcelen;
	uint32_t ucs4;
	uint8_t *pucs4 = (uint8_t *) &ucs4;
	size_t sucs4 = 4;
	parserutils_error error;

	/* Use iconv to convert a single character
	 * Side effect: Updates *source to point at next input
	 * character and *sourcelen to reflect reduced input length
	 */
	iconv_ret = iconv(c->read_cd, (char **) source, sourcelen,
			(char **) (void *) &pucs4, &sucs4);

	if (iconv_ret != (size_t) -1 ||
			(*source != origsrc && sucs4 == 0)) {
		/* Read a character */
		error = iconv_codec_output_decoded_char(c, ucs4, dest, destlen);
		if (error != PARSERUTILS_OK && error != PARSERUTILS_NOMEM) {
			/* output failed; restore source pointers */
			*source = origsrc;
			*sourcelen = origsrclen;
		}

		/* Clear inval buffer */
		c->inval_buf[0] = '\0';
		c->inval_len = 0;

		return error;
	} else if (errno == E2BIG) {
		/* Should never happen */
		abort();
	} else if (errno == EINVAL) {
		/* Incomplete input sequence */
		if (*sourcelen > INVAL_BUFSIZE)
			abort();

		memmove(c->inval_buf, *source, *sourcelen);
		c->inval_buf[*sourcelen] = '\0';
		c->inval_len = *sourcelen;

		*source += *sourcelen;
		*sourcelen = 0;

		return PARSERUTILS_OK;
	} else if (errno == EILSEQ) {
		/* Illegal input sequence */
		bool found = false;
		const uint8_t *oldsrc;
		size_t oldsrclen;

		/* Clear inval buffer */
		c->inval_buf[0] = '\0';
		c->inval_len = 0;

		/* Strict errormode; simply flag invalid character */
		if (c->base.errormode == 
				PARSERUTILS_CHARSET_CODEC_ERROR_STRICT) {
			/* restore source pointers */
			*source = origsrc;
			*sourcelen = origsrclen;

			return PARSERUTILS_INVALID;
		}

		/* Ok, this becomes problematic. The iconv API here
		* is particularly unhelpful; *source will point at
		* the _start_ of the illegal sequence. This means
		* that we must find the end of the sequence */

		/* Search for the start of the next valid input
		 * sequence (or the end of the input stream) */
		while (*sourcelen > 1) {
			pucs4 = (uint8_t *) &ucs4;
			sucs4 = 4;

			(*source)++;
			(*sourcelen)--;

			oldsrc = *source;
			oldsrclen = *sourcelen;

			iconv_ret = iconv(c->read_cd,
					(char **) source, sourcelen,
					(char **) (void *) &pucs4, &sucs4);
			if (iconv_ret != (size_t) -1 || errno != EILSEQ) {
				found = true;
				break;
			}
		}

		if (found) {
			/* Found start of next valid sequence */
			*source = oldsrc;
			*sourcelen = oldsrclen;
		} else {
			/* Not found - skip last byte in buffer */
			(*source)++;
			(*sourcelen)--;

			if (*sourcelen != 0)
				abort();
		}

		/* output U+FFFD and continue processing. */
		error = iconv_codec_output_decoded_char(c,
				htonl(0xFFFD), dest, destlen);
		if (error != PARSERUTILS_OK && error != PARSERUTILS_NOMEM) {
			/* output failed; restore source pointers */
			*source = origsrc;
			*sourcelen = origsrclen;
		}

		return error;
	}

	return PARSERUTILS_OK;
}

/**
 * Write a UCS-4 character in a codec's native charset
 *
 * \param c        The codec
 * \param ucs4     The UCS-4 character to write (big endian)
 * \param dest     Pointer to pointer to output buffer (updated on exit)
 * \param destlen  Pointer to length of output buffer (updated on exit)
 * \return PARSERUTILS_OK       on success,
 *         PARSERUTILS_NOMEM    if output buffer is too small,
 *         PARSERUTILS_INVALID  if character cannot be represented and the
 *                         codec's error handling mode is set to STRICT.
 */
parserutils_error iconv_codec_write_char(iconv_codec *c,
		uint32_t ucs4, uint8_t **dest, size_t *destlen)
{
	size_t iconv_ret;
	uint8_t *pucs4 = (uint8_t *) &ucs4;
	size_t sucs4 = 4;
	uint8_t *origdest = *dest;

	iconv_ret = iconv(c->write_cd, (char **) (void *) &pucs4,
			&sucs4, (char **) dest, destlen);

	if (iconv_ret == (size_t) -1 && errno == E2BIG) {
		/* Output buffer is too small */
		return PARSERUTILS_NOMEM;
	} else if (iconv_ret == (size_t) -1 && errno == EILSEQ) {
		/* Illegal multibyte sequence */
		/* This should never happen */
		abort();
	} else if (iconv_ret == (size_t) -1 && errno == EINVAL) {
		/* Incomplete input character */
		/* This should never happen */
		abort();
	} else if (*dest == origdest) {
		/* Nothing was output */
		switch (c->base.errormode) {
		case PARSERUTILS_CHARSET_CODEC_ERROR_STRICT:
			return PARSERUTILS_INVALID;

		case PARSERUTILS_CHARSET_CODEC_ERROR_TRANSLIT:
			/** \todo transliteration */
		case PARSERUTILS_CHARSET_CODEC_ERROR_LOOSE:
		{
			pucs4 = (uint8_t *) &ucs4;
			sucs4 = 4;

			ucs4 = parserutils_charset_mibenum_is_unicode(
					c->base.mibenum)
					? htonl(0xFFFD) : htonl(0x3F);

			iconv_ret = iconv(c->write_cd,
					(char **) (void *) &pucs4, &sucs4,
					(char **) dest, destlen);

			if (iconv_ret == (size_t) -1 && errno == E2BIG) {
				return PARSERUTILS_NOMEM;
			} else if (iconv_ret == (size_t) -1 &&
					errno == EILSEQ) {
				/* Illegal multibyte sequence */
				/* This should never happen */
				abort();
			} else if (iconv_ret == (size_t) -1 &&
					errno == EINVAL) {
				/* Incomplete input character */
				/* This should never happen */
				abort();
			}
		}
			break;
		}
	}

	return PARSERUTILS_OK;
}

const parserutils_charset_handler iconv_codec_handler = {
	iconv_codec_handles_charset,
	iconv_codec_create
};

#endif
