#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <parserutils/utils/dict.h>

#include "testutils.h"

extern void parserutils_dict_dump(parserutils_dict *dict);

static void *myrealloc(void *ptr, size_t len, void *pw)
{
	UNUSED(pw);

	return realloc(ptr, len);
}

int main(int argc, char **argv)
{
	parserutils_dict *dict;
	uint8_t buf[256];

	UNUSED(argc);
	UNUSED(argv);

	/* Seed buffer with printable ascii */
	for (int i = 0; i < (int) sizeof(buf); i++) {
		buf[i] = 97 + (int) (26.0 * (rand() / (RAND_MAX + 1.0)));
	}
	buf[sizeof(buf) - 1] = '\0';

	assert(parserutils_dict_create(myrealloc, NULL, &dict) == 
			PARSERUTILS_OK);

	for (int i = 0; i < (int) sizeof(buf); i++) {
		uint8_t *s = buf;

		while (s - buf <= i) {
			const parserutils_dict_entry *e;

			parserutils_dict_insert(dict, 
					s, (size_t) (sizeof(buf) - i), &e);

			s++;
		}
	}

	parserutils_dict_destroy(dict);

	printf("PASS\n");

	return 0;
}

