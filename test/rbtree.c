#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/rbtree.h"

#include "testutils.h"

static void *myrealloc(void *ptr, size_t len, void *pw)
{
	UNUSED(pw);

	return realloc(ptr, len);
}

static int mycmp(const void *a, const void *b)
{
	return ((const uint32_t) a) - ((const uint32_t) b);
}

int main(int argc, char **argv)
{
	parserutils_rbtree *tree;

	UNUSED(argc);
	UNUSED(argv);

	tree = parserutils_rbtree_create(mycmp, myrealloc, NULL);
	assert(tree != NULL);

#define N 40000
#define G 307
//#define N 400
//#define G 7

	printf("Inserting %d items\n", N);

	for (int i = G, count = 1; i != 0; i = (i + G) % N, count++) {
		void *old;
		assert(parserutils_rbtree_insert(tree, (void *) i, (void *) i, 
				&old) == PARSERUTILS_OK);

		if ((count % 10000) == 0)
			printf("%d\n", count);
	}

	printf("Removing %d items\n", N/2);

	for (int i = 1, count = 1; i < N; i += 2, count++) {
		void *key, *value;
		assert(parserutils_rbtree_delete(tree, (void *) i, &key,
				&value) == PARSERUTILS_OK);
		if ((count % 10000) == 0)
			printf("%d\n", count);
	}

	printf("Finding %d items\n", N/2);

	for (int i = 2, count = 1; i < N; i += 2, count++) {
		void *value = NULL;
		assert(parserutils_rbtree_find(tree, (void *) i, &value) == 
				PARSERUTILS_OK);
		assert(value != NULL && ((int) value) == i);
		if ((count % 10000) == 0)
			printf("%d\n", count);
	}

	printf("Verifying & removing %d items\n", N/2);

	for (int i = 1, count = 1; i < N; i += 2, count++) {
		void *key, *value = NULL;
		assert(parserutils_rbtree_find(tree, (void *) i, &value) == 
				PARSERUTILS_OK);
		assert(value == NULL);
		assert(parserutils_rbtree_delete(tree, (void *) i,
				&key, &value) == PARSERUTILS_OK);
		if ((count % 10000) == 0)
			printf("%d\n", count);
	}

	parserutils_rbtree_destroy(tree, NULL, NULL);

	printf("PASS\n");

	return 0;
}

