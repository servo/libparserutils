/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <inttypes.h>
#include <string.h>

#include <parserutils/utils/vector.h>

/**
 * Vector object
 */
struct parserutils_vector
{
	size_t item_size;		/**< Size of an item in the vector */
	size_t chunk_size;		/**< Size of a vector chunk */
	size_t items_allocated;		/**< Number of slots allocated */
	int32_t current_item;		/**< Index of current item */
	void *items;			/**< Items in vector */

	parserutils_alloc alloc;	/**< Memory (de)allocation function */
	void *pw;			/**< Client-specific data */
};

/**
 * Create a vector
 *
 * \param item_size   Length, in bytes, of an item in the vector
 * \param chunk_size  Number of vector slots in a chunk
 * \param alloc       Memory (de)allocation function
 * \param pw          Pointer to client-specific private data
 * \return Pointer to vector instance, or NULL on memory exhaustion
 */
parserutils_vector *parserutils_vector_create(size_t item_size, 
		size_t chunk_size, parserutils_alloc alloc, void *pw)
{
	parserutils_vector *vector;

	if (item_size == 0 || chunk_size == 0 || alloc == NULL)
		return NULL;

	vector = alloc(NULL, sizeof(parserutils_vector), pw);
	if (vector == NULL)
		return NULL;

	vector->items = alloc(NULL, item_size * chunk_size, pw);
	if (vector->items == NULL) {
		alloc(vector, 0, pw);
		return NULL;
	}

	vector->item_size = item_size;
	vector->chunk_size = chunk_size;
	vector->items_allocated = chunk_size;
	vector->current_item = -1;

	vector->alloc = alloc;
	vector->pw = pw;

	return vector;
}

/**
 * Destroy a vector instance
 *
 * \param vector  The vector to destroy
 */
void parserutils_vector_destroy(parserutils_vector *vector)
{
	if (vector == NULL)
		return;

	vector->alloc(vector->items, 0, vector->pw);
	vector->alloc(vector, 0, vector->pw);
}

/**
 * Append an item to the vector
 *
 * \param vector  The vector to append to
 * \param item    The item to append
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_vector_append(parserutils_vector *vector, 
		void *item)
{
	int32_t slot;

	if (vector == NULL || item == NULL)
		return PARSERUTILS_BADPARM;

	/* Ensure we'll get a valid slot */
	if (vector->current_item < -1 || vector->current_item == INT32_MAX)
		return PARSERUTILS_INVALID;

	slot = vector->current_item + 1;

	if ((size_t) slot >= vector->items_allocated) {
		void *temp = vector->alloc(vector->items,
				(vector->items_allocated + vector->chunk_size) *
				vector->item_size, vector->pw);
		if (temp == NULL)
			return PARSERUTILS_NOMEM;

		vector->items = temp;
		vector->items_allocated += vector->chunk_size;
	}

	memcpy((uint8_t *) vector->items + (slot * vector->item_size), 
			item, vector->item_size);
	vector->current_item = slot;

	return PARSERUTILS_OK;
}

/**
 * Clear a vector
 *
 * \param vector  The vector to clear
 * \return PARSERUTILS_OK on success, appropriate error otherwise.
 */
parserutils_error parserutils_vector_clear(parserutils_vector *vector)
{
	if (vector == NULL)
		return PARSERUTILS_BADPARM;

	if (vector->current_item < 0)
		return PARSERUTILS_INVALID;

	vector->current_item = -1;

	return PARSERUTILS_OK;
}

/**
 * Remove the last item from a vector
 *
 * \param vector  The vector to remove from
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_vector_remove_last(parserutils_vector *vector)
{
	if (vector == NULL)
		return PARSERUTILS_BADPARM;

	if (vector->current_item < 0)
		return PARSERUTILS_INVALID;

	vector->current_item--;

	return PARSERUTILS_OK;
}

/**
 * Iterate over a vector
 *
 * \param vector  The vector to iterate over
 * \param ctx     Pointer to an integer for the iterator to use as context.
 * \return Pointer to current item, or NULL if no more
 */
void *parserutils_vector_iterate(parserutils_vector *vector, int32_t *ctx)
{
	void *item;

	if (vector == NULL || ctx == NULL || vector->current_item < 0)
		return NULL;

	if ((*ctx) > vector->current_item)
		return NULL;

	item = (uint8_t *) vector->items + ((*ctx) * vector->item_size);

	(*ctx)++;

	return item;
}

#ifndef NDEBUG
#include <stdio.h>

extern void parserutils_vector_dump(parserutils_vector *vector, 
		const char *prefix, void (*printer)(void *item));

void parserutils_vector_dump(parserutils_vector *vector, const char *prefix,
		void (*printer)(void *item))
{
	if (vector == NULL || printer == NULL)
		return;

	for (int32_t i = 0; i <= vector->current_item; i++) {
		printf("%s %d: ", prefix != NULL ? prefix : "", i);
		printer((uint8_t *) vector->items + (i * vector->item_size));
		printf("\n");
	}
}

#endif

