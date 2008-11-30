/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <stdio.h>
#include <string.h>

#include "utils/chunkarray.h"

typedef struct chunk chunk;

struct chunk {
#define CHUNK_SIZE (4096)
	chunk *next;

	uint32_t used;

	uint8_t data[CHUNK_SIZE];
};

struct parserutils_chunkarray {
	chunk *used_chunks;

	chunk *free_chunks;

	parserutils_alloc alloc;
	void *pw;
};

/**
 * Create a chunked array
 *
 * \param alloc  Memory (de)allocation function
 * \param pw     Pointer to client-specific private data
 * \param array  Pointer to location to receive array instance
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_chunkarray_create(parserutils_alloc alloc, 
		void *pw, parserutils_chunkarray **array)
{
	parserutils_chunkarray *c;

	if (alloc == NULL || array == NULL)
		return PARSERUTILS_BADPARM;

	c = alloc(0, sizeof(parserutils_chunkarray), pw);
	if (c == NULL)
		return PARSERUTILS_NOMEM;

	c->used_chunks = NULL;
	c->free_chunks = NULL;

	c->alloc = alloc;
	c->pw = pw;

	*array = c;

	return PARSERUTILS_OK;
}

/**
 * Destroy a chunked array
 *
 * \param array  The array to destroy
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_chunkarray_destroy(parserutils_chunkarray *array)
{
	chunk *c, *d;

	if (array == NULL)
		return PARSERUTILS_BADPARM;

	for (c = array->used_chunks; c != NULL; c = d) {
		d = c->next;
		array->alloc(c, 0, array->pw);
	}

	for (c = array->free_chunks; c != NULL; c = d) {
		d = c->next;
		array->alloc(c, 0, array->pw);
	}

	array->alloc(array, 0, array->pw);

	return PARSERUTILS_OK;
}

/**
 * Insert an item into an array
 *
 * \param array     The array to insert into
 * \param data      Pointer to data to insert
 * \param len       Length, in bytes, of data
 * \param inserted  Pointer to location to receive pointer to inserted data
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_chunkarray_insert(parserutils_chunkarray *array,
		const void *data, size_t len, const void **inserted)
{
	if (array == NULL || data == NULL || inserted == NULL)
		return PARSERUTILS_BADPARM;

	/* If we're trying to insert data larger than CHUNK_SIZE, then it 
	 * gets its own chunk. */
	if (len > CHUNK_SIZE) {
		chunk *c = array->alloc(0, sizeof(chunk) + len - CHUNK_SIZE,
				array->pw);
		if (c == NULL)
			return PARSERUTILS_NOMEM;

		/* Populate chunk */
		(*inserted) = c->data;
		memcpy(c->data, data, len);
		c->used = len;

		/* And put it in the used list */
		c->next = array->used_chunks;
		array->used_chunks = c;
	} else {
		/* Scan the free list until we find a chunk with enough space */
		chunk *c, *prev;

		for (prev = NULL, c = array->free_chunks; c != NULL; 
				prev = c, c = c->next) {
			if (CHUNK_SIZE - c->used >= len)
				break;
		}

		if (c == NULL) {
			/* None big enough, create a new one */
			c = array->alloc(0, sizeof(chunk), array->pw);
			if (c == NULL)
				return PARSERUTILS_NOMEM;

			c->used = 0;

			/* Insert it at the head of the free list */
			c->next = array->free_chunks;
			array->free_chunks = c;

			/* And ensure that prev is kept in sync */
			prev = NULL;
		}

		/* Populate chunk */
		(*inserted) = c->data + c->used;
		memcpy(c->data + c->used, data, len);
		c->used += len;

		/* If we've now filled the chunk, move it to the used list */
		if (c->used == CHUNK_SIZE) {
			if (prev != NULL)
				prev->next = c->next;
			else
				array->free_chunks = c->next;

			c->next = array->used_chunks;
			array->used_chunks = c;
		}
	}

	return PARSERUTILS_OK;
}

/******************************************************************************
 * Private functions                                                          *
 ******************************************************************************/

extern void parserutils_chunkarray_dump(parserutils_chunkarray *array);

/**
 * Dump details of a chunked array to stdout
 *
 * \param array  The array to dump
 */
void parserutils_chunkarray_dump(parserutils_chunkarray *array)
{
	uint32_t n = 0;
	size_t count = 0;
	size_t total = sizeof(parserutils_chunkarray);

	for (chunk *c = array->used_chunks; c != NULL; c = c->next) {
		n++;
		count += c->used;
		if (c->used == CHUNK_SIZE)
			total += sizeof(chunk);
		else
			total += sizeof(chunk) + c->used - CHUNK_SIZE;
	}

	printf("%u full blocks: %zu bytes\n", n, count);

	n = 0;
	count = 0;

	for (chunk *c = array->free_chunks; c != NULL; c = c->next) {
		n++;
		count += c->used;
		total += sizeof(chunk);
	}

	printf("%u partial blocks: %zu bytes (of %u => %f%%)\n", n, count, 
			n * CHUNK_SIZE, 
			(count * 100) / ((float) n * CHUNK_SIZE));

	printf("Total: %zu (%lu) (%lu)\n",  total, sizeof(chunk), 
			sizeof(parserutils_chunkarray));
}

