/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <stdio.h>
#include <string.h>

#include <parserutils/utils/hash.h>

#include "utils/chunkarray.h"

typedef struct slot_table slot_table;

struct parserutils_hash {
	slot_table *slots;

	parserutils_chunkarray *data;

	parserutils_alloc alloc;
	void *pw;
};

struct slot_table {
#define DEFAULT_SLOTS (1<<6)
	size_t n_slots;
	size_t n_used;

	const parserutils_chunkarray_entry *slots[];
};

static inline uint32_t _hash(const uint8_t *data, size_t len);
static inline int _cmp(const uint8_t *a, size_t alen, 
		const uint8_t *b, size_t blen);
static inline void grow_slots(parserutils_hash *hash);

/**
 * Create a hash
 *
 * \param alloc  Memory (de)allocation function
 * \param pw     Pointer to client-specific private data
 * \param hash   Pointer to location to receive result
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_hash_create(parserutils_alloc alloc, void *pw,
		parserutils_hash **hash)
{
	parserutils_hash *h;
	parserutils_error error;

	if (alloc == NULL || hash == NULL)
		return PARSERUTILS_BADPARM;

	h = alloc(0, sizeof(parserutils_hash), pw);
	if (h == NULL)
		return PARSERUTILS_NOMEM;

	h->slots = alloc(0, sizeof(slot_table) + 
			DEFAULT_SLOTS * sizeof(parserutils_chunkarray_entry *),
			pw);
	if (h->slots == NULL) {
		alloc(h, 0, pw);
		return PARSERUTILS_NOMEM;
	}

	memset(h->slots, 0, sizeof(slot_table) + 
			DEFAULT_SLOTS * sizeof(parserutils_chunkarray_entry *));
	h->slots->n_slots = DEFAULT_SLOTS;

	error = parserutils_chunkarray_create(alloc, pw, &h->data);
	if (error != PARSERUTILS_OK) {
		alloc(h->slots, 0, pw);
		alloc(h, 0, pw);
		return error;
	}

	h->alloc = alloc;
	h->pw = pw;

	*hash = h;

	return PARSERUTILS_OK;
}

/**
 * Destroy a hash
 *
 * \param hash  The hash to destroy
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_hash_destroy(parserutils_hash *hash)
{
	if (hash == NULL)
		return PARSERUTILS_BADPARM;

	parserutils_chunkarray_destroy(hash->data);

	hash->alloc(hash->slots, 0, hash->pw);

	hash->alloc(hash, 0, hash->pw);

	return PARSERUTILS_OK;
}

/**
 * Insert an item into a hash
 *
 * \param hash      The hash to insert into
 * \param data      Pointer to data
 * \param len       Length, in bytes, of data
 * \param inserted  Pointer to location to receive pointer to data in hash
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_hash_insert(parserutils_hash *hash,
		const uint8_t *data, uint16_t len,
		const parserutils_hash_entry **inserted)
{
	uint32_t index, mask;
	slot_table *slots;
	const parserutils_chunkarray_entry **entries;
	const parserutils_chunkarray_entry *entry;
	parserutils_error error;

	if (hash == NULL || data == NULL || inserted == NULL)
		return PARSERUTILS_BADPARM;

	slots = hash->slots;
	entries = slots->slots;

	/* Find index */
	mask = slots->n_slots - 1;
	index = _hash(data, len) & mask;

	/* Use linear probing to resolve collisions */
	while ((entry = entries[index]) != NULL) {
		/* If this data is already in the hash, return it */
		if (_cmp(data, len, entry->data, entry->length) == 0) {
			(*inserted) = (const parserutils_hash_entry *) entry;
			return PARSERUTILS_OK;
		}

		index = (index + 1) & mask;
	}

	/* The data isn't in the hash. Index identifies the slot to use */
	error = parserutils_chunkarray_insert(hash->data, data, len, &entry);
	if (error != PARSERUTILS_OK)
		return error;

	entries[index] = entry;
	(*inserted) = (const parserutils_hash_entry *) entry;

	/* If the table is 75% full, then increase its size */
	if (++slots->n_used >= (slots->n_slots >> 1) + (slots->n_slots >> 2))
		grow_slots(hash);

	return PARSERUTILS_OK;
}

/******************************************************************************
 * Private functions                                                          *
 ******************************************************************************/

/**
 * Hsieh hash function
 *
 * \param data  Pointer to data to hash
 * \param len   Length, in bytes, of data
 * \return hash value
 */
uint32_t _hash(const uint8_t *data, size_t len)
{
	uint32_t hash = len, tmp;
	int rem = len & 3;

#define read16(d) ((((uint32_t)((d)[1])) << 8) | ((uint32_t)((d)[0])))

	for (len = len >> 2; len > 0; len--) {
		hash += read16(data);
		tmp   = (read16(data + 2) << 11) ^ hash;
		hash  = (hash << 16) ^ tmp;
		data += 4;
		hash += hash >> 11;
	}

	switch (rem) {
	case 3:
		hash += read16(data);
		hash ^= hash << 16;
		hash ^= data[2] << 18;
		hash += hash >> 11;
		break;
	case 2:
		hash += read16(data);
		hash ^= hash << 11;
		hash += hash >> 17;
		break;
	case 1:
		hash += data[0];
		hash ^= hash << 10;
		hash += hash >> 1;
	}

#undef read16

	hash ^= hash << 3;
	hash += hash >> 5;
	hash ^= hash << 4;
	hash += hash >> 17;
	hash ^= hash << 25;
	hash += hash >> 6;

	return hash;
}

/**
 * Comparator for hash entries
 *
 * \param a     First item to consider 
 * \param alen  Length, in bytes, of a
 * \param b     Second item to consider
 * \param blen  Length, in bytes, of b
 * \return <0 iff a<b, ==0 iff a=b, >0 iff a>b
 */
int _cmp(const uint8_t *a, size_t alen, const uint8_t *b, size_t blen)
{
	int result;

	/* Check for identity first */
	if (a == b)
		return 0;

	/* Sort first by length, then by data equality */
	if ((result = alen - blen) == 0)
		result = memcmp(a, b, alen);

	return result;
}

/**
 * Increase the size of the slot table
 *
 * \param hash  The hash to process
 */
void grow_slots(parserutils_hash *hash)
{
	slot_table *s;
	size_t new_size;

	if (hash == NULL)
		return;

	new_size = hash->slots->n_slots << 1;

	/* Create new slot table */
	s = hash->alloc(0, sizeof(slot_table) + 
			new_size * sizeof(parserutils_chunkarray_entry *),
			hash->pw);
	if (s == NULL)
		return;

	memset(s, 0, sizeof(slot_table) + 
			new_size * sizeof(parserutils_chunkarray_entry *));
	s->n_slots = new_size;

	/* Now, populate it with the contents of the current one */
	for (uint32_t i = 0; i < hash->slots->n_slots; i++) {
		const parserutils_chunkarray_entry *e = hash->slots->slots[i];

		if (e == NULL)
			continue;

		/* Find new index */
		uint32_t mask = s->n_slots - 1;
		uint32_t index = _hash(e->data, e->length) & mask;

		/* Use linear probing to resolve collisions */
		while (s->slots[index] != NULL)
			index = (index + 1) & mask;

		/* Insert into new slot table */
		s->slots[index] = e;
		s->n_used++;
	}

	/* Destroy current table, and replace it with the new one */
	hash->alloc(hash->slots, 0, hash->pw);
	hash->slots = s;

	return;
}

extern void parserutils_chunkarray_dump(parserutils_chunkarray *array);
extern void parserutils_hash_dump(parserutils_hash *hash);

/**
 * Dump details of a hash to stdout
 *
 * \param hash  The hash to dump
 */
void parserutils_hash_dump(parserutils_hash *hash)
{
	printf("%zu slots used (of %zu => %f%%)\n", hash->slots->n_used,
		hash->slots->n_slots, 
		hash->slots->n_used * 100 / (float) hash->slots->n_slots);

	printf("Data:\n");
	parserutils_chunkarray_dump(hash->data);

	printf("Hash structures: %zu\n", 
		sizeof(parserutils_hash) + sizeof(slot_table) + 
		hash->slots->n_slots * sizeof(parserutils_chunkarray_entry *));
}

