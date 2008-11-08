#include <stdio.h>
#include <stdlib.h>

#include <parserutils/parserutils.h>

#include "input/filter.h"

#include "testutils.h"

static void *myrealloc(void *ptr, size_t len, void *pw)
{
	UNUSED(pw);

	return realloc(ptr, len);
}

int main(int argc, char **argv)
{
	parserutils_filter *input;

	if (argc != 2) {
		printf("Usage: %s <filename>\n", argv[0]);
		return 1;
	}

	assert(parserutils_initialise(argv[1], myrealloc, NULL) == 
			PARSERUTILS_OK);

	assert(parserutils_filter_create("UTF-8", myrealloc, NULL, &input) ==
			PARSERUTILS_OK);

	parserutils_filter_destroy(input);

	assert(parserutils_finalise(myrealloc, NULL) == PARSERUTILS_OK);

	printf("PASS\n");

	return 0;
}
