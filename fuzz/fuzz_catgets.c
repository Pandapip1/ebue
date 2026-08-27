/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * catopen()/catgets()/catclose() and nl_langinfo(), added after the
 * original fuzzing inventory.  The first half writes arbitrary bytes as
 * a catalogue and makes the real reader validate them.  The second half
 * builds the smallest useful valid catalogue from fuzzer data, ensuring
 * that lookup and close are reached even before libFuzzer learns the
 * catalogue magic and offset relationships.  A fuzzer-built NLSPATH is
 * tried before a final known component, which drives every expansion
 * keyword while preserving a route to the catalogue.
 */
#include <nl_types.h>
#include <langinfo.h>
#include <locale.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

extern void oracle_mismatch_i(const char *, const char *, long long, long long);
extern void oracle_mismatch_s(const char *, const char *, const char *, const char *);

#define PATH "./fuzz-catgets.cat"
#define RAWMAX 512

static int write_file(const unsigned char *p, size_t n)
{
	int fd = open(PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) return -1;
	while (n) {
		ssize_t w = write(fd, p, n);
		if (w < 0) { if (errno == EINTR) continue; close(fd); return -1; }
		if (w == 0) { close(fd); return -1; }
		p += w;
		n -= (size_t)w;
	}
	return close(fd);
}

static void put32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)(v >> 24);
	p[1] = (unsigned char)(v >> 16);
	p[2] = (unsigned char)(v >> 8);
	p[3] = (unsigned char)v;
}

static void query(nl_catd cd, int set, int msg)
{
	static const char fallback[] = "fuzz fallback";
	char *s;

	errno = 0;
	s = catgets(cd, set, msg, fallback);
	if (s == fallback) {
		if (errno != ENOMSG)
			oracle_mismatch_i("catgets miss errno", fallback, errno, ENOMSG);
	} else {
		/* Reading the complete result is intentional: accepted catalogue
		 * offsets must always identify a terminated in-range string. */
		volatile size_t len = strlen(s);
		(void)len;
	}
}

static void raw_catalog(const unsigned char *data, size_t size)
{
	nl_catd cd;
	uint32_t a = 0, b = 0;

	if (size > RAWMAX) size = RAWMAX;
	if (size >= 4) memcpy(&a, data, 4);
	if (size >= 8) memcpy(&b, data + 4, 4);
	if (write_file(data, size) != 0) return;
	cd = catopen(PATH, NL_CAT_LOCALE);
	if (cd == (nl_catd)-1) return;
	query(cd, (int)a, (int)b);
	query(cd, 1, 1);
	if (catclose(cd) != 0)
		oracle_mismatch_i("catclose(raw) failed", PATH, errno, 0);
}

static void valid_catalog(const unsigned char *data, size_t size)
{
	unsigned char cat[20 + 12 + 24 + 130];
	char one[65], two[65];
	size_t n = size < 128 ? size : 128;
	size_t n1 = n / 2, n2 = n - n1, i;
	size_t pooloff = 12 + 24;
	size_t body, total;
	nl_catd cd;
	char *got;

	for (i = 0; i < n1; i++) one[i] = data[i] ? (char)data[i] : 'x';
	for (i = 0; i < n2; i++) two[i] = data[n1 + i] ? (char)data[n1 + i] : 'y';
	one[n1] = 0;
	two[n2] = 0;
	body = pooloff + n1 + 1 + n2 + 1;
	total = 20 + body;
	memset(cat, 0, sizeof cat);

	put32(cat + 0, 0xff88ff89u);
	put32(cat + 4, 1);                 /* one set */
	put32(cat + 8, (uint32_t)body);
	put32(cat + 12, 12);               /* messages after set table */
	put32(cat + 16, (uint32_t)pooloff);
	put32(cat + 20, 1);                /* set 1, two messages, first 0 */
	put32(cat + 24, 2);
	put32(cat + 32, 0);
	put32(cat + 32 + 0, 1);            /* message 1 */
	put32(cat + 32 + 4, (uint32_t)n1);
	put32(cat + 32 + 8, 0);
	put32(cat + 44 + 0, 2);            /* message 2 */
	put32(cat + 44 + 4, (uint32_t)n2);
	put32(cat + 44 + 8, (uint32_t)(n1 + 1));
	memcpy(cat + 20 + pooloff, one, n1 + 1);
	memcpy(cat + 20 + pooloff + n1 + 1, two, n2 + 1);

	if (write_file(cat, total) != 0) return;
	cd = catopen(PATH, NL_CAT_LOCALE);
	if (cd == (nl_catd)-1)
		oracle_mismatch_i("catopen rejected a valid catalogue", PATH, errno, 0);
	if (cd == (nl_catd)-1) return;
	got = catgets(cd, 1, 1, "missing");
	if (strcmp(got, one) != 0)
		oracle_mismatch_s("catgets message 1", PATH, got, one);
	got = catgets(cd, 1, 2, "missing");
	if (strcmp(got, two) != 0)
		oracle_mismatch_s("catgets message 2", PATH, got, two);
	query(cd, 1, 3);
	if (catclose(cd) != 0)
		oracle_mismatch_i("catclose(valid) failed", PATH, errno, 0);
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char nlspath[196], lang[66];
	size_t pn, ln, i;
	nl_catd cd;
	nl_item item = 0;
	char *a, *b;
	locale_t loc;

	if (!size) return 0;
	raw_catalog(data, size);
	valid_catalog(data, size);

	/* Arbitrary templates first; :./%N is the known final component. */
	pn = size < 188 ? size : 188;
	for (i = 0; i < pn; i++) nlspath[i] = data[i] ? (char)data[i] : '_';
	memcpy(nlspath + pn, ":./%N", 6);
	nlspath[pn + 6] = 0;
	ln = size < 65 ? size : 65;
	for (i = 0; i < ln; i++) lang[i] = data[size - ln + i] ?
	                                      (char)data[size - ln + i] : '_';
	lang[ln] = 0;
	(void)setenv("NLSPATH", nlspath, 1);
	(void)setenv("LANG", lang, 1);
	cd = catopen("fuzz-catgets.cat", 0);
	if (cd != (nl_catd)-1) {
		query(cd, 1, 1);
		(void)catclose(cd);
	}

	if (size >= sizeof item) memcpy(&item, data, sizeof item);
	a = nl_langinfo(item);
	if (!a) oracle_mismatch_i("nl_langinfo returned NULL", "", item, 0);
	else (void)strlen(a);
	loc = newlocale(LC_ALL_MASK, "C", (locale_t)0);
	if (loc) {
		b = nl_langinfo_l(item, loc);
		if (!b || strcmp(a, b) != 0)
			oracle_mismatch_s("nl_langinfo_l differs in C locale", "", a,
			                  b ? b : "(null)");
		freelocale(loc);
	}
	return 0;
}
