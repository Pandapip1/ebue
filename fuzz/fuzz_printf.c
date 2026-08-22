/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The real src/stdio/printf.c, driven by a fuzzer-built format string.
 *
 * Two design decisions matter here.
 *
 * First, the *real* printf.c is compiled and linked, with its real
 * internal buffers.  An earlier attempt copied a few hundred lines of it
 * into a standalone harness and modelled a 4096-byte output buffer; that
 * harness could not reach the `char body[256]` overflow that a plain
 * snprintf("%f", DBL_MAX) triggers, and so reported nothing.
 *
 * Second, the format string is not raw fuzzer bytes.  Feeding random text
 * to printf is undefined behaviour by construction -- a %s whose argument
 * was never passed -- and every "crash" would be the harness's fault.
 * Instead the fuzz data drives a small grammar: the conversion is chosen
 * from a fixed set, then flags, width, precision and a length modifier
 * legal for that conversion, and finally an argument of exactly the right
 * type from the same input.  Any crash here is printf's.
 *
 * %n is excluded: it writes through a caller-supplied pointer, which is a
 * question about the caller rather than about formatting.
 *
 * Where glibc is a valid oracle the output is compared against it; where
 * the standard leaves the spelling open (%p, %a's choice of significand)
 * only the "did not write past the buffer" check applies.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern int host_snprintf_1d(char *, size_t, const char *, double);
extern int host_snprintf_1ll(char *, size_t, const char *, long long);
extern int host_snprintf_1s(char *, size_t, const char *, const char *);
extern void oracle_mismatch_s(const char *, const char *, const char *, const char *);
extern void oracle_mismatch_i(const char *, const char *, long long, long long);

struct rd { const unsigned char *p; size_t n, i; };
static unsigned char u8(struct rd *r) { return r->i < r->n ? r->p[r->i++] : 0; }
static unsigned long long u64(struct rd *r)
{
	unsigned long long v = 0; int k;
	for (k = 0; k < 8; k++) v = (v << 8) | u8(r);
	return v;
}

/* Written out by hand rather than with sprintf: the harness must not
 * depend on the function it is testing. */
static size_t put_uint(char *p, unsigned v)
{
	char t[12];
	size_t k = 0, j = 0;
	do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
	while (k) p[j++] = t[--k];
	return j;
}

static const char CONV[]  = "diouxXeEfFgGaAcsp";
static const char FLAGS[] = "-+ #0";
static const char *INTMOD[] = { "", "h", "hh", "l", "ll", "z", "j", "t" };

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	struct rd r = { data, size, 0 };
	char fmt[64];
	char out[2048], ref[2048];
	char strarg[64];
	int rounds, i;

	if (size < 4) return 0;
	rounds = 1 + (u8(&r) % 4);

	for (i = 0; i < rounds; i++) {
		char conv = CONV[u8(&r) % (sizeof CONV - 1)];
		size_t k = 0, cap = 1 + (size_t)(u8(&r) % 64);   /* small caps on purpose */
		unsigned char b;
		int n, hn = -2, j, nflags;

		fmt[k++] = '%';
		nflags = u8(&r) % 4;
		for (j = 0; j < nflags; j++)
			fmt[k++] = FLAGS[u8(&r) % (sizeof FLAGS - 1)];

		b = u8(&r);
		if (b & 1) k += put_uint(fmt + k, (unsigned)((u8(&r) | (u8(&r) << 8)) % 400));
		if (b & 4) {
			fmt[k++] = '.';
			if (b & 8) k += put_uint(fmt + k, (unsigned)(u8(&r) % 400));
		}
		if (strchr("diouxX", conv)) {
			const char *m = INTMOD[u8(&r) % 8];
			while (*m) fmt[k++] = *m++;
		} else if (strchr("eEfFgGaA", conv) && (u8(&r) & 1)) {
			fmt[k++] = 'l';       /* %lf == %f, and printf.c must know it */
		}
		fmt[k++] = conv;
		fmt[k] = 0;

		memset(out, 0x7f, sizeof out);
		memset(ref, 0x7f, sizeof ref);

		switch (conv) {
		case 'd': case 'i': case 'o': case 'u': case 'x': case 'X': {
			long long v = (long long)u64(&r);
			n  = snprintf(out, cap, fmt, v);
			hn = host_snprintf_1ll(ref, cap, fmt, v);
			break;
		}
		case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
		case 'a': case 'A': {
			unsigned long long bits = u64(&r);
			double v;
			memcpy(&v, &bits, 8);
			n  = snprintf(out, cap, fmt, v);
			hn = host_snprintf_1d(ref, cap, fmt, v);
			/* %a may legally pick a different hex significand. */
			if (conv == 'a' || conv == 'A') hn = -2;
			break;
		}
		case 'c': {
			long long v = (long long)(unsigned char)u8(&r);
			n  = snprintf(out, cap, fmt, v);
			hn = host_snprintf_1ll(ref, cap, fmt, v);
			break;
		}
		case 's': {
			size_t m = 0, want = u8(&r) % (sizeof strarg - 1);
			while (m < want) { unsigned char c = u8(&r); strarg[m++] = c ? (char)c : 'x'; }
			strarg[m] = 0;
			n  = snprintf(out, cap, fmt, strarg);
			hn = host_snprintf_1s(ref, cap, fmt, strarg);
			break;
		}
		default: {         /* 'p' -- spelling is implementation defined */
			void *v = (void *)(unsigned long)(u64(&r) & 0xffffffffULL);
			n = snprintf(out, cap, fmt, v);
			break;
		}
		}

		/* Whatever it returns, snprintf must not have touched out[cap]. */
		if (out[cap] != 0x7f)
			oracle_mismatch_i("snprintf wrote past its size limit", fmt,
			                  (long long)(unsigned char)out[cap], 0x7f);
		/* ...and must always NUL-terminate within it. */
		if (memchr(out, 0, cap) == 0)
			oracle_mismatch_i("snprintf left the buffer unterminated", fmt, 0, 1);

		if (hn != -2) {
			if (n != hn)
				oracle_mismatch_i("snprintf return value", fmt, n, hn);
			if (memcmp(out, ref, cap) != 0)
				oracle_mismatch_s("snprintf output", fmt, out, ref);
		}
	}
	return 0;
}
