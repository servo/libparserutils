/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef parserutils_utils_rbtree_h_
#define parserutils_utils_rbtree_h_

#include <parserutils/errors.h>
#include <parserutils/functypes.h>

struct parserutils_rbtree;
typedef struct parserutils_rbtree parserutils_rbtree;

typedef int (*parserutils_rbtree_cmp)(const void *a, const void *b);
typedef void (*parserutils_rbtree_del)(void *key, void *value, void *pw);
typedef void (*parserutils_rbtree_print)(const void *key, const void *value,
		int depth);

parserutils_error parserutils_rbtree_create(parserutils_rbtree_cmp cmp,
		parserutils_alloc alloc, void *pw, parserutils_rbtree **tree);
parserutils_error parserutils_rbtree_destroy(parserutils_rbtree *tree,
		parserutils_rbtree_del destructor, void *pw);

parserutils_error parserutils_rbtree_insert(parserutils_rbtree *tree, 
		void *key, void *value, void **oldvalue);
parserutils_error parserutils_rbtree_find(parserutils_rbtree *tree,
		const void *key, void **value);
parserutils_error parserutils_rbtree_delete(parserutils_rbtree *tree,
		const void *key, void **intkey, void **value);

#ifndef NDEBUG
void parserutils_rbtree_dump(parserutils_rbtree *tree, 
		parserutils_rbtree_print print);
#endif

#endif

