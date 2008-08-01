/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <assert.h>
#include <string.h>

#include <parserutils/utils/dict.h>

#include "utils/rbtree.h"
#include "utils/utils.h"

/**
 * Dictionary object
 */
struct parserutils_dict
{
#define TABLE_SIZE (79)
	parserutils_rbtree *table[TABLE_SIZE]; /**< Hashtable of entries */

	parserutils_alloc alloc;	/**< Memory (de)allocation function */
	void *pw;			/**< Client-specific data */
};

static inline uint32_t dict_hash(const uint8_t *data, size_t len);
static int dict_cmp(const void *a, const void *b);
static void dict_del(void *key, void *value, void *pw);

/**
 * Create a dictionary
 *
 * \param alloc  Memory (de)allocation function
 * \param pw     Pointer to client-specific private data
 * \return Pointer to dictionary instance, or NULL on memory exhaustion
 */
parserutils_dict *parserutils_dict_create(parserutils_alloc alloc, void *pw)
{
	parserutils_dict *dict;

	if (alloc == NULL)
		return NULL;

	dict = alloc(NULL, sizeof(parserutils_dict), pw);
	if (dict == NULL)
		return NULL;

	memset(dict->table, 0, TABLE_SIZE * sizeof(parserutils_rbtree *));

	dict->alloc = alloc;
	dict->pw = pw;

	return dict;
}

/**
 * Destroy a dictionary
 *
 * \param dict  The dictionary instance to destroy
 */
void parserutils_dict_destroy(parserutils_dict *dict)
{
	for (int i = 0; i < TABLE_SIZE; i++) {
		parserutils_rbtree_destroy(dict->table[i], dict_del, dict);
	}

	dict->alloc(dict, 0, dict->pw);
}

/**
 * Insert an item into a dictionary
 *
 * \param dict    The dictionary to insert into
 * \param data    Pointer to data
 * \param len     Length, in bytes, of data
 * \param result  Pointer to location to receive pointer to data in dictionary
 */
parserutils_error parserutils_dict_insert(parserutils_dict *dict,
	       const uint8_t *data, size_t len, 
	       const parserutils_dict_entry **result)
{
	parserutils_error error;
	parserutils_dict_entry *entry = NULL;
	void *old_value;
	uint32_t index;

	if (dict == NULL || data == NULL || len == 0)
		return PARSERUTILS_BADPARM;

	index = dict_hash(data, len) % TABLE_SIZE;

	if (dict->table[index] != NULL) {
		parserutils_dict_entry search = { len, (uint8_t *) data };

		error = parserutils_rbtree_find(dict->table[index], 
				(void *) &search, (void *) &entry);
		if (error != PARSERUTILS_OK)
			return error;

		if (entry != NULL) {
			*result = entry;
			return PARSERUTILS_OK;
		}
	} else {
		dict->table[index] = parserutils_rbtree_create(dict_cmp, 
				dict->alloc, dict->pw);
		if (dict->table[index] == NULL)
			return PARSERUTILS_NOMEM;
	}

	entry = dict->alloc(NULL, sizeof(parserutils_dict_entry) + len, 
			dict->pw);
	if (entry == NULL)
		return PARSERUTILS_NOMEM;

	/* Do-it-yourself variable-sized data member (simplifies the 
	 * manufacture of an entry to search for, above) */
	memcpy(((uint8_t *) entry) + sizeof(parserutils_dict_entry), data, len);
	entry->data = ((uint8_t *) entry) + sizeof(parserutils_dict_entry);
	entry->len = len;

	error = parserutils_rbtree_insert(dict->table[index], (void *) entry, 
			(void *) entry, &old_value);
	if (error != PARSERUTILS_OK) {
		dict->alloc(entry, 0, dict->pw);
		return error;
	}
	assert(old_value == NULL);

	*result = entry;

	return PARSERUTILS_OK;
}

/**
 * Hsieh hash function
 *
 * \param data  Pointer to data to hash
 * \param len   Length, in bytes, of data
 * \return hash value
 */
uint32_t dict_hash(const uint8_t *data, size_t len)
{
	uint32_t hash = len, tmp;
	int rem = len & 3;

#define read16(d) ((((uint32_t)((d)[1])) << 8) | ((uint32_t)((d)[0])))

	for (len = len / 4; len > 0; len--) {
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
 * Comparison function for dictionary entries
 *
 * \param a  First entry to consider
 * \param b  Second entry to consider
 * \return <0 iff a<b, ==0 iff a=b, >0 iff a>b
 */
int dict_cmp(const void *a, const void *b)
{
	const parserutils_dict_entry *aa = (const parserutils_dict_entry *) a;
	const parserutils_dict_entry *bb = (const parserutils_dict_entry *) b;
	int result = aa->len - bb->len;

	/* Sort first by length, and then by data equality */
	if (result == 0) {
		result = memcmp(aa->data, bb->data, aa->len);
	}

	return result;
}

/**
 * Destructor for dictionary entries
 *
 * \param key    Key
 * \param value  Value
 * \param pw     Dictionary instance
 */
void dict_del(void *key, void *value, void *pw)
{
	parserutils_dict *dict = (parserutils_dict *) pw;

	UNUSED(value);

	dict->alloc(key, 0, dict->pw);
}

#ifndef NDEBUG
#include <stdio.h>

extern void parserutils_dict_dump(parserutils_dict *dict);

/**
 * Print out a dictionary entry
 *
 * \param key    The key
 * \param value  The value
 * \param depth  Depth in tree
 */
static void dict_print(const void *key, const void *value, int depth)
{
	const parserutils_dict_entry *e = (const parserutils_dict_entry *) key;

	UNUSED(value);

	while (depth-- > 0)
		putchar(' ');

	printf("'%.*s'\n", (int)e->len, e->data);
}

/**
 * Print out a dictionary
 * 
 * \param dict  The dictionary to print
 */
void parserutils_dict_dump(parserutils_dict *dict)
{
	if (dict == NULL)
		return;

	for (int i = 0; i < TABLE_SIZE; i++) {
		printf("%d:\n", i);
		parserutils_rbtree_dump(dict->table[i], dict_print);
	}
}
#endif

