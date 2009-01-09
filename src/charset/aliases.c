/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2007 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assert.h>

#include "charset/aliases.h"
#include "utils/utils.h"

struct alias {
	struct alias *next;
	parserutils_charset_aliases_canon *canon;
	uint16_t name_len;
	char name[1];
};

#define HASH_SIZE (43)
static parserutils_charset_aliases_canon *canon_tab[HASH_SIZE];
static struct alias *alias_tab[HASH_SIZE];

static parserutils_error parserutils_charset_create_alias(const char *alias,
		parserutils_charset_aliases_canon *c, 
		parserutils_alloc alloc, void *pw);
static parserutils_charset_aliases_canon *parserutils_charset_create_canon(
		const char *canon, uint16_t mibenum, 
		parserutils_alloc alloc, void *pw);
static int aliascmp(const char *s1, const char *s2, size_t s2_len);
static uint32_t parserutils_charset_hash_val(const char *alias, size_t len);

/**
 * Create alias data from Aliases file
 *
 * \param filename  The path to the Aliases file
 * \param alloc     Memory (de)allocation function
 * \param pw        Pointer to client-specific private data (may be NULL)
 * \return PARSERUTILS_OK on success, appropriate error otherwise.
 */
parserutils_error parserutils_charset_aliases_create(const char *filename,
		parserutils_alloc alloc, void *pw)
{
	char buf[300];
	FILE *fp;

	if (filename == NULL || alloc == NULL)
		return PARSERUTILS_BADPARM;

	fp = fopen(filename, "r");
	if (fp == NULL)
		return PARSERUTILS_FILENOTFOUND;

	while (fgets(buf, sizeof buf, fp)) {
		char *p, *aliases = 0, *mib, *end;
		parserutils_charset_aliases_canon *cf;

		if (buf[0] == 0 || buf[0] == '#')
			/* skip blank lines or comments */
			continue;

		buf[strlen(buf) - 1] = 0; /* lose terminating newline */
		end = buf + strlen(buf);

		/* find end of canonical form */
		for (p = buf; *p && !isspace(*p) && !iscntrl(*p); p++)
			; /* do nothing */
		if (p >= end)
			continue;
		*p++ = '\0'; /* terminate canonical form */

		/* skip whitespace */
		for (; *p && isspace(*p); p++)
			; /* do nothing */
		if (p >= end)
			continue;
		mib = p;

		/* find end of mibenum */
		for (; *p && !isspace(*p) && !iscntrl(*p); p++)
			; /* do nothing */
		if (p < end)
			*p++ = '\0'; /* terminate mibenum */

		cf = parserutils_charset_create_canon(buf, atoi(mib), alloc, pw);
		if (cf == NULL)
			continue;

		/* skip whitespace */
		for (; p < end && *p && isspace(*p); p++)
			; /* do nothing */
		if (p >= end)
			continue;
		aliases = p;

		while (p < end) {
			/* find end of alias */
			for (; *p && !isspace(*p) && !iscntrl(*p); p++)
				; /* do nothing */
			if (p > end)
				/* stop if we've gone past the end */
				break;
			/* terminate current alias */
			*p++ = '\0';

			if (parserutils_charset_create_alias(aliases, cf,
					alloc, pw) != PARSERUTILS_OK)
				break;

			/* in terminating, we may have advanced
			 * past the end - check this here */
			if (p >= end)
				break;

			/* skip whitespace */
			for (; *p && isspace(*p); p++)
				; /* do nothing */

			if (p >= end)
				/* gone past end => stop */
				break;

			/* update pointer to current alias */
			aliases = p;
		}
	}

	fclose(fp);

	return PARSERUTILS_OK;
}

/**
 * Free all alias data
 *
 * \param alloc  Memory (de)allocation function
 * \param pw     Pointer to client-specific private data
 */
void parserutils_charset_aliases_destroy(parserutils_alloc alloc, void *pw)
{
	parserutils_charset_aliases_canon *c, *d;
	struct alias *a, *b;
	int i;

	for (i = 0; i != HASH_SIZE; i++) {
		for (c = canon_tab[i]; c; c = d) {
			d = c->next;
			alloc(c, 0, pw);
		}
		canon_tab[i] = NULL;

		for (a = alias_tab[i]; a; a = b) {
			b = a->next;
			alloc(a, 0, pw);
		}
		alias_tab[i] = NULL;
	}
}

/**
 * Retrieve the MIB enum value assigned to an encoding name
 *
 * \param alias  The alias to lookup
 * \param len    The length of the alias string
 * \return The MIB enum value, or 0 if not found
 */
uint16_t parserutils_charset_mibenum_from_name(const char *alias, size_t len)
{
	parserutils_charset_aliases_canon *c;

	if (alias == NULL)
		return 0;

	c = parserutils_charset_alias_canonicalise(alias, len);
	if (c == NULL)
		return 0;

	return c->mib_enum;
}

/**
 * Retrieve the canonical name of an encoding from the MIB enum
 *
 * \param mibenum The MIB enum value
 * \return Pointer to canonical name, or NULL if not found
 */
const char *parserutils_charset_mibenum_to_name(uint16_t mibenum)
{
	int i;
	parserutils_charset_aliases_canon *c;

	for (i = 0; i != HASH_SIZE; i++)
		for (c = canon_tab[i]; c; c = c->next)
			if (c->mib_enum == mibenum)
				return c->name;

	return NULL;
}

/**
 * Detect if a parserutils_charset is Unicode
 *
 * \param mibenum  The MIB enum to consider
 * \return true if a Unicode variant, false otherwise
 */
bool parserutils_charset_mibenum_is_unicode(uint16_t mibenum)
{
	static uint16_t ucs4;
	static uint16_t ucs2;
	static uint16_t utf8;
	static uint16_t utf16;
	static uint16_t utf16be;
	static uint16_t utf16le;
	static uint16_t utf32;
	static uint16_t utf32be;
	static uint16_t utf32le;

	if (ucs4 == 0) {
		ucs4 = parserutils_charset_mibenum_from_name("UCS-4", 
				SLEN("UCS-4"));
		ucs2 = parserutils_charset_mibenum_from_name("UCS-2", 
				SLEN("UCS-2"));
		utf8 = parserutils_charset_mibenum_from_name("UTF-8", 
				SLEN("UTF-8"));
		utf16 = parserutils_charset_mibenum_from_name("UTF-16", 
				SLEN("UTF-16"));
		utf16be = parserutils_charset_mibenum_from_name("UTF-16BE",
				SLEN("UTF-16BE"));
		utf16le = parserutils_charset_mibenum_from_name("UTF-16LE",
				SLEN("UTF-16LE"));
		utf32 = parserutils_charset_mibenum_from_name("UTF-32", 
				SLEN("UTF-32"));
		utf32be = parserutils_charset_mibenum_from_name("UTF-32BE",
				SLEN("UTF-32BE"));
		utf32le = parserutils_charset_mibenum_from_name("UTF-32LE",
				SLEN("UTF-32LE"));
	}

	return (mibenum == ucs4 || mibenum == ucs2 || mibenum == utf8 ||
			mibenum == utf16 || mibenum == utf16be || 
			mibenum == utf16le || mibenum == utf32 ||
			mibenum == utf32be || mibenum == utf32le);
}

#define IS_PUNCT_OR_SPACE(x) \
		((0x09 <= (x) && (x) <= 0x0D) || \
				(0x20 <= (x) && (x) <= 0x2F) || \
				(0x3A <= (x) && (x) <= 0x40) || \
				(0x5B <= (x) && (x) <= 0x60) || \
				(0x7B <= (x) && (x) <= 0x7E))


/**
 * Compare name "s1" to name "s2" (of size s2_len) case-insensitively
 * and ignoring ASCII punctuation characters.
 *
 * See http://www.whatwg.org/specs/web-apps/current-work/#character0
 *
 * \param s1		Alias to compare to
 * \param s2		Alias to compare
 * \param s2_len	Length of "s2"
 * \returns 0 if equal, 1 otherwise
 */
int aliascmp(const char *s1, const char *s2, size_t s2_len)
{
	size_t s2_pos = 0;

	if (s1 == NULL || s2_len == 0)
		return 1;

	while (true) {
		while (IS_PUNCT_OR_SPACE(*s1))
			s1++;
		while (s2_pos < s2_len &&
				IS_PUNCT_OR_SPACE(s2[s2_pos])) {
			s2_pos++;
		}

		if (s2_pos == s2_len)
			return (*s1 != '\0') ? 1 : 0;

		if (tolower(*s1) != tolower(s2[s2_pos]))
			break;
		s1++;
		s2_pos++;
	}

	return 1;
}


/**
 * Retrieve the canonical form of an alias name
 *
 * \param alias  The alias name
 * \param len    The length of the alias name
 * \return Pointer to canonical form or NULL if not found
 */
parserutils_charset_aliases_canon *parserutils_charset_alias_canonicalise(
		const char *alias, size_t len)
{
	uint32_t hash;
	parserutils_charset_aliases_canon *c;
	struct alias *a;

	if (alias == NULL)
		return NULL;

	hash = parserutils_charset_hash_val(alias, len);

	for (c = canon_tab[hash]; c; c = c->next)
		if (aliascmp(c->name, alias, len) == 0)
			break;
	if (c)
		return c;

	for (a = alias_tab[hash]; a; a = a->next)
		if (aliascmp(a->name, alias, len) == 0)
			break;
	if (a)
		return a->canon;

	return NULL;
}


/**
 * Create an alias
 *
 * \param alias  The alias name
 * \param c      The canonical form
 * \param alloc  Memory (de)allocation function
 * \param pw     Pointer to client-specific private data (may be NULL)
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_charset_create_alias(const char *alias, 
		parserutils_charset_aliases_canon *c,
		parserutils_alloc alloc, void *pw)
{
	struct alias *a;
	uint32_t hash;

	if (alias == NULL || c == NULL || alloc == NULL)
		return PARSERUTILS_BADPARM;

	a = alloc(NULL, sizeof(struct alias) + strlen(alias) + 1, pw);
	if (a == NULL)
		return PARSERUTILS_NOMEM;

	a->canon = c;
	a->name_len = strlen(alias);
	strcpy(a->name, alias);
	a->name[a->name_len] = '\0';

	hash = parserutils_charset_hash_val(alias, a->name_len);

	a->next = alias_tab[hash];
	alias_tab[hash] = a;

	return PARSERUTILS_OK;
}

/**
 * Create a canonical form
 *
 * \param canon    The canonical name
 * \param mibenum  The MIB enum value
 * \param alloc    Memory (de)allocation function
 * \param pw       Pointer to client-specific private data (may be NULL)
 * \return Pointer to canonical form or NULL on error
 */
parserutils_charset_aliases_canon *parserutils_charset_create_canon(
		const char *canon, uint16_t mibenum, 
		parserutils_alloc alloc, void *pw)
{
	parserutils_charset_aliases_canon *c;
	uint32_t hash, len;

	if (canon == NULL || alloc == NULL)
		return NULL;

	len = strlen(canon);

	c = alloc(NULL, sizeof(parserutils_charset_aliases_canon) + len + 1, pw);
	if (c == NULL)
		return NULL;

	c->mib_enum = mibenum;
	c->name_len = len;
	strcpy(c->name, canon);
	c->name[len] = '\0';

	hash = parserutils_charset_hash_val(canon, len);

	c->next = canon_tab[hash];
	canon_tab[hash] = c;

	return c;
}

/**
 * Hash function
 *
 * \param alias  String to hash
 * \param len    Number of bytes to hash (<= strlen(alias))
 * \return The hashed value
 */
uint32_t parserutils_charset_hash_val(const char *alias, size_t len)
{
	const char *s = alias;
	uint32_t h = 5381;

	if (alias == NULL)
		return 0;

	while (len--) {
		if (IS_PUNCT_OR_SPACE(*s)) {
			s++;
		} else {
			h = (h * 33) ^ (*s++ & ~0x20); /* case insensitive */
		}
	}

	return h % HASH_SIZE;
}


#ifndef NDEBUG
/**
 * Dump all alias data to stdout
 */
void parserutils_charset_aliases_dump(void)
{
	parserutils_charset_aliases_canon *c;
	struct alias *a;
	int i;
	size_t size = 0;

	for (i = 0; i != HASH_SIZE; i++) {
		for (c = canon_tab[i]; c; c = c->next) {
			printf("%d %s\n", i, c->name);
			size += offsetof(parserutils_charset_aliases_canon, 
					name) + c->name_len;
		}

		for (a = alias_tab[i]; a; a = a->next) {
			printf("%d %s\n", i, a->name);
			size += offsetof(struct alias, name) + a->name_len;
		}
	}

	size += (sizeof(canon_tab) / sizeof(canon_tab[0]));
	size += (sizeof(alias_tab) / sizeof(alias_tab[0]));

	printf("%u\n", (unsigned int) size);
}
#endif
