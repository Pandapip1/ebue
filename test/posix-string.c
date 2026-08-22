/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of <string.h>/<strings.h>
 * requirements not already exercised by test/string.c's sanity pass.
 * Each block cites the page it was checked against under
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* strncpy.html DESCRIPTION: "stpncpy() ... shall return ... a pointer to
 * the trailing NUL character, or if it does not append such a NUL
 * character, ... &s1[n]."  i.e. when the source has no NUL within the
 * first n bytes, no NUL is written and the return value is s1+n. */
static void test_stpncpy_no_nul_return(void)
{
	char buf[8];
	char *r;

	memset(buf, 'z', sizeof buf);
	r = stpncpy(buf, "abcdef", 3);	/* "abcdef" has no NUL in first 3 bytes */
	CHECK(r == buf + 3);
	CHECK(buf[0] == 'a' && buf[1] == 'b' && buf[2] == 'c' && buf[3] == 'z');
}

/* strtok.html APPLICATION USAGE: "If sep is the empty string, strtok()
 * and strtok_r() return a pointer to the remainder of the string being
 * tokenized." */
static void test_strtok_empty_sep(void)
{
	char buf[16];
	char *sp;

	strcpy(buf, "hello");
	CHECK(!strcmp(strtok(buf, ""), "hello"));

	strcpy(buf, "world");
	CHECK(!strcmp(strtok_r(buf, "", &sp), "world"));
}

/* strxfrm.html DESCRIPTION: "The strxfrm() and strxfrm_l() functions
 * shall not change the setting of errno if successful." */
static void test_strxfrm_errno_preserved(void)
{
	char buf[32];

	errno = 0x5a5a;
	strxfrm(buf, "hello", sizeof buf);
	CHECK(errno == 0x5a5a);
}

/* strerror.html DESCRIPTION: "The strerror() and strerror_l() functions
 * shall not change the setting of errno if successful." */
static void test_strerror_errno_preserved(void)
{
	errno = 0x5a5a;
	(void)strerror(ENOENT);
	CHECK(errno == 0x5a5a);
}

/* memcmp.html DESCRIPTION: bytes are "interpreted as unsigned char" for
 * both the comparison and the sign of the result -- already asserted in
 * test/string.c via memcmp("\xff","\x01",1) > 0.  Here: n == 0 must
 * report equality without needing to look at either object. */
static void test_memcmp_zero_length(void)
{
	CHECK(memcmp("a", "b", 0) == 0);
}

/* memchr.html / memset.html-style zero-length calls: searching/copying
 * zero bytes must not find anything and must not fault on n == 0. */
static void test_memccpy_zero_length(void)
{
	char dst[4];

	dst[0] = 'x';
	CHECK(memccpy(dst, "hello", 'h', 0) == 0);	/* 'h' not among the 0 bytes copied */
	CHECK(dst[0] == 'x');				/* nothing written */
}

/* strcspn.html / a matching strspn.html clause: the "reject set" (resp.
 * "accept set") being the whole alphabet used, or empty, are boundary
 * cases already covered in test/string.c; here: a single-byte s1. */
static void test_strspn_strcspn_single_byte(void)
{
	CHECK(strspn("a", "a") == 1);
	CHECK(strspn("a", "b") == 0);
	CHECK(strcspn("a", "a") == 0);
	CHECK(strcspn("a", "b") == 1);
}

/* strcasecmp.html DESCRIPTION: comparison result sign reflects s1 vs s2
 * ignoring case, not just a boolean equal/not-equal -- verify ordering,
 * not just the zero/nonzero cases test/string.c already checks. */
static void test_strcasecmp_ordering(void)
{
	CHECK(strcasecmp("APPLE", "banana") < 0);
	CHECK(strcasecmp("banana", "APPLE") > 0);
	CHECK(strncasecmp("APPLE", "banana", 5) < 0);
}

int main(void)
{
	test_stpncpy_no_nul_return();
	test_strtok_empty_sep();
	test_strxfrm_errno_preserved();
	test_strerror_errno_preserved();
	test_memcmp_zero_length();
	test_memccpy_zero_length();
	test_strspn_strcspn_single_byte();
	test_strcasecmp_ordering();

	if (!fails)
		printf("posix-string: all tests passed\n");
	return fails != 0;
}
