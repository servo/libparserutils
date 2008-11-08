#include <stdio.h>

#include "charset/charset.h"
#include <parserutils/charset/codec.h>

#include "testutils.h"

static void *myrealloc(void *ptr, size_t len, void *pw)
{
	UNUSED(pw);

	return realloc(ptr, len);
}

int main(int argc, char **argv)
{
	parserutils_charset_codec *codec;

	if (argc != 2) {
		printf("Usage: %s <aliases_file>\n", argv[0]);
		return 1;
	}

	assert(parserutils_charset_initialise(argv[1], myrealloc, NULL) == 
			PARSERUTILS_OK);

	assert(parserutils_charset_codec_create("UTF-8", myrealloc, NULL,
			&codec) == PARSERUTILS_OK);

	parserutils_charset_codec_destroy(codec);

	assert(parserutils_charset_finalise(myrealloc, NULL) == 
			PARSERUTILS_OK);

	printf("PASS\n");

	return 0;
}
