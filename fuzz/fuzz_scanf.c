/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The real src/stdio/scanf.c, driven by a fuzzer-built format and a
 * fuzzer-built input string.
 *
 * As with the printf harness, the format is generated from a grammar
 * rather than taken raw: sscanf with a format the caller did not supply
 * arguments for is undefined by construction, so raw formats would only
 * find the harness's own bugs.  Here every conversion gets a real,
 * correctly typed destination.  %s, %c and %[ always carry a field width
 * -- without one, an over-long input overflows the destination and that
 * is the caller's fault, not scanf's.
 *
 * The *input* string is raw fuzzer bytes.  That is the interesting axis:
 * no malformed input may make sscanf write outside what it was given.
 *
 * Each destination is followed by a poisoned tail inside the same object.
 * ASan cannot flag a write there for itself -- it is not out of bounds --
 * so the harness checks it explicitly.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

struct rd { const unsigned char *p; size_t n, i; };
static unsigned char u8(struct rd *r) { return r->i < r->n ? r->p[r->i++] : 0; }

static size_t put_uint(char *p, unsigned v)
{
	char t[12];
	size_t k = 0, j = 0;
	do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
	while (k) p[j++] = t[--k];
	return j;
}

#define NDST  4
#define DSTSZ 128
#define TAIL  16
#define MAXW  64            /* field widths stay well inside DSTSZ */

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	struct rd r = { data, size, 0 };
	char fmt[160];
	char in[512];
	static char dst[NDST][DSTSZ + TAIL];
	size_t k = 0, inlen;
	int nconv, i;

	if (size < 8) return 0;

	nconv = 1 + (u8(&r) % NDST);
	for (i = 0; i < nconv; i++) {
		char conv = "diouxXeEfgGscn[" [u8(&r) % 15];
		unsigned char b = u8(&r);

		if (b & 1) fmt[k++] = ' ';                           /* whitespace */
		if (b & 2) fmt[k++] = (char)('a' + (u8(&r) % 26));   /* literal */
		fmt[k++] = '%';

		switch (conv) {
		case 'd': case 'i': case 'o': case 'u': case 'x': case 'X':
			if (b & 8) k += put_uint(fmt + k, (unsigned)(1 + u8(&r) % MAXW));
			fmt[k++] = 'l'; fmt[k++] = 'l'; fmt[k++] = conv;
			break;
		case 'e': case 'E': case 'f': case 'g': case 'G':
			if (b & 8) k += put_uint(fmt + k, (unsigned)(1 + u8(&r) % MAXW));
			fmt[k++] = 'l'; fmt[k++] = conv;
			break;
		case 'n':
			fmt[k++] = 'n';
			break;
		case '[':
			k += put_uint(fmt + k, (unsigned)(1 + u8(&r) % MAXW));
			fmt[k++] = '[';
			if (u8(&r) & 1) fmt[k++] = '^';
			fmt[k++] = 'a'; fmt[k++] = '-'; fmt[k++] = 'z'; fmt[k++] = ']';
			break;
		case 'c':
			k += put_uint(fmt + k, (unsigned)(1 + u8(&r) % MAXW));
			fmt[k++] = 'c';
			break;
		default:
			k += put_uint(fmt + k, (unsigned)(1 + u8(&r) % MAXW));
			fmt[k++] = 's';
			break;
		}
		memset(dst[i], 0x5a, sizeof dst[i]);
	}
	fmt[k] = 0;

	inlen = r.n - r.i;
	if (inlen > sizeof in - 1) inlen = sizeof in - 1;
	memcpy(in, r.p + r.i, inlen);
	in[inlen] = 0;

	switch (nconv) {
	case 1: (void)sscanf(in, fmt, dst[0]); break;
	case 2: (void)sscanf(in, fmt, dst[0], dst[1]); break;
	case 3: (void)sscanf(in, fmt, dst[0], dst[1], dst[2]); break;
	default: (void)sscanf(in, fmt, dst[0], dst[1], dst[2], dst[3]); break;
	}

	for (i = 0; i < nconv; i++) {
		size_t j;
		for (j = DSTSZ; j < DSTSZ + TAIL; j++)
			if (dst[i][j] != 0x5a)
				oracle_mismatch_i("sscanf wrote past the destination", fmt,
				                  (long long)(unsigned char)dst[i][j], 0x5a);
	}
	return 0;
}
