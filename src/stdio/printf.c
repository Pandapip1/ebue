/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __vfprintf: the one formatter every printf/fprintf/sprintf/snprintf
 * variant calls into.  sprintf and snprintf are __vfprintf writing into
 * a throwaway FILE that is a fixed-size memory buffer exactly like
 * fmemopen's (see mem.c and __file_write in buf.c) but built on the
 * stack, so truncation and buffer-filling logic is not written twice.
 *
 * Floating-point conversions (%f/%e/%g and their capitals) are exact.
 * A finite double is m * 2^e for integers m < 2^53 and e, so its value
 * is a binary rational and its decimal expansion terminates: dec_exact
 * below computes every digit of it with one big-integer multiply, and
 * dec_round then rounds that expansion to the requested number of
 * places, to nearest with ties to even.  Every digit printed is
 * therefore the digit glibc and musl print.  %a/%A are exact for the
 * same reason and more directly: a double's significand is already 13
 * hex digits, so they are read straight out of the bits.
 * Positional (%n$) arguments are not implemented; nothing in this tree
 * uses them.
 *
 * No conversion sizes anything from the caller's precision, which C99
 * 7.19.6.1 leaves unbounded -- see PREC_MAX below.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <errno.h>
#include "stdio_impl.h"

enum { LM_NONE, LM_hh, LM_h, LM_l, LM_ll, LM_j, LM_z, LM_t, LM_L };

/* A precision is an int and C99 7.19.6.1 puts no bound on it, so no
 * buffer may be sized from one.  PREC_MAX is the largest precision
 * fmt_f/fmt_e ever write out in full: the exact expansion of a double
 * runs to at most 767 significant digits and its last fractional place
 * is the 1074th (the smallest subnormal is 2^-1074), so every place
 * past PREC_MAX is a zero no matter what the value is.  emit_float
 * formats with the precision clamped to PREC_MAX and streams the
 * dropped zeros straight out, which makes the body length independent
 * of the caller's precision. */
#define PREC_MAX 1080
/* Worst-case body: 309 integer digits (DBL_MAX at %f), a point and
 * PREC_MAX fractional digits -- 1390 bytes, rounded up.  The other
 * shapes are smaller: %e is one digit, a point, PREC_MAX more and a
 * five-byte exponent, and %g hands fmt_f a precision of P-1-decexp,
 * which makes its body PREC_MAX+1 whatever decexp is. */
#define BODYMAX 1536

/* Write n bytes to f, tracking the logical (untruncated) total in
 * *count.  A short write is a real error unless f is a fixed memory
 * buffer (sprintf/snprintf), in which case it is just truncation. */
static void out(FILE *f, const char *s, size_t n, long *count, int *bad)
{
	if (*bad) return;
	if (n && __fwrite(s, 1, n, f) != n) {
		if (!f->is_mem || f->mem_dynamic) { f->err = 1; *bad = 1; return; }
	}
	*count += (long)n;
}

static void pad(FILE *f, char c, size_t n, long *count, int *bad)
{
	char buf[16];
	memset(buf, c, sizeof buf);
	while (n && !*bad) {
		size_t k = n < sizeof buf ? n : sizeof buf;
		out(f, buf, k, count, bad);
		n -= k;
	}
}

/* ---- exact decimal expansion of a double ---------------------------- */

/* Big non-negative integers in base 10^9, least significant limb first,
 * so that reading the decimal digits back out is a matter of splitting
 * limbs rather than dividing a binary big integer down.  Only mul_small
 * is needed: nothing here ever adds, subtracts or divides.
 *
 * DEC_LIMBS is chosen so nothing can overflow it.  The widest value
 * formed below is (2^53-1) * 5^1074, the numerator of the smallest
 * subnormals, which has 767 digits and so 86 limbs; the widest of the
 * other case, 2^1024, has 309 digits.  EXACT_DIG is the matching bound
 * on the digits themselves. */
#define DEC_LIMBS 88
#define EXACT_DIG 768

/* a = a * m, for m small enough that limb * m + carry stays inside a
 * uint64 (every m used here is below 2^30). */
static int mul_small(uint32_t *a, int n, uint32_t m)
{
	uint64_t carry = 0;
	int i;

	for (i = 0; i < n; i++) {
		uint64_t t = (uint64_t)a[i] * m + carry;
		a[i] = (uint32_t)(t % 1000000000u);
		carry = t / 1000000000u;
	}
	/* cannot run past DEC_LIMBS; do not corrupt memory if it does */
	while (carry && n < DEC_LIMBS) {
		a[n++] = (uint32_t)(carry % 1000000000u);
		carry /= 1000000000u;
	}
	return n;
}

/* The exact decimal expansion of a finite v >= 0: d[0..nd) are its
 * significant digits, and d[0] is the 10^decexp place, so the value is
 * 0.d * 10^(decexp+1) with every place past d[nd-1] a zero.  nd is
 * never more than EXACT_DIG, and d[0] is '0' only for v == 0. */
struct dec {
	int nd;
	int decexp;
	char d[EXACT_DIG];
};

static void dec_exact(double v, struct dec *D)
{
	union { double f; uint64_t i; } u;
	uint32_t bn[DEC_LIMBS];
	uint64_t m;
	int e2, bl = 0, k, i, j, nfrac = 0;
	char *p;

	u.f = v;
	e2 = (int)(u.i >> 52 & 0x7ff);
	m = u.i & 0xfffffffffffffULL;
	if (e2) { m |= (uint64_t)1 << 52; e2 -= 1075; }
	else e2 = -1074;   /* subnormal: no implicit bit, the same scale */

	if (!m) { D->nd = 1; D->decexp = 0; D->d[0] = '0'; return; }
	while (m) { bn[bl++] = (uint32_t)(m % 1000000000u); m /= 1000000000u; }

	/* v = m * 2^e2.  For e2 >= 0 that is the integer m << e2; for
	 * e2 < 0 it is m * 5^-e2 with the point -e2 places from the right,
	 * since m / 2^k == m * 5^k / 10^k.  Either way one big integer
	 * carries every digit, so no division is needed to produce them. */
	while (e2 > 0) {
		k = e2 > 29 ? 29 : e2;
		bl = mul_small(bn, bl, 1u << k);
		e2 -= k;
	}
	if (e2 < 0) {
		nfrac = -e2;
		for (k = nfrac; k > 0; ) {
			if (k >= 12) { bl = mul_small(bn, bl, 244140625u); k -= 12; }  /* 5^12 */
			else {
				uint32_t f = 1;
				while (k--) f *= 5;
				bl = mul_small(bn, bl, f);
			}
		}
	}

	p = D->d;
	{
		uint32_t hi = bn[bl - 1];
		char t[10];
		i = 0;
		do { t[i++] = (char)('0' + (int)(hi % 10)); hi /= 10; } while (hi);
		while (i) *p++ = t[--i];
	}
	for (i = bl - 2; i >= 0; i--) {
		uint32_t w = bn[i];
		for (j = 8; j >= 0; j--) { p[j] = (char)('0' + (int)(w % 10)); w /= 10; }
		p += 9;
	}
	D->nd = (int)(p - D->d);
	D->decexp = D->nd - 1 - nfrac;
	/* trailing zeros are implicit anyway, and dropping them keeps the
	 * "is the discarded tail nonzero" test in dec_round short */
	while (D->nd > 1 && D->d[D->nd - 1] == '0') D->nd--;
}

/* Round D to want >= 1 significant digits, to nearest with ties to
 * even.  Asking for more digits than the expansion has is a no-op: the
 * rest are zeros already, and every reader here treats an index past nd
 * as a zero.  A carry out of the leading digit bumps decexp, leaving
 * "1" followed by zeros. */
static void dec_round(struct dec *D, int want)
{
	int i, up;

	if (want >= D->nd) return;
	up = D->d[want] > '5';
	if (D->d[want] == '5') {
		for (i = want + 1; i < D->nd; i++) if (D->d[i] != '0') { up = 1; break; }
		if (!up) up = (D->d[want - 1] - '0') & 1;   /* a tie goes to even */
	}
	D->nd = want;
	if (!up) {
		while (D->nd > 1 && D->d[D->nd - 1] == '0') D->nd--;
		return;
	}
	for (i = want - 1; i >= 0; i--) {
		if (D->d[i] != '9') { D->d[i]++; D->nd = i + 1; return; }
		D->d[i] = '0';
	}
	D->d[0] = '1';
	D->nd = 1;
	D->decexp++;
}

/* %f-style body (no sign): pos digits before the point, then a point
 * and prec digits after it, from D rounded to decexp+1+prec significant
 * digits.  When that count is not positive the whole value sits below
 * the last place shown, and the result is a zero there -- or a one, if
 * the value reaches half of it. */
static int fmt_f(char *buf, struct dec *D, int prec, int alt)
{
	int want = D->decexp + 1 + prec, pos, i, n = 0;

	if (want >= 1) dec_round(D, want);
	else {
		/* want == 0 puts the leading digit exactly one place below the
		 * last one shown, so the value rounds up to a one there when
		 * that digit is past 5, or is 5 with anything nonzero after
		 * it; an exact half ties to the even zero.  want < 0 puts it
		 * further down still, which always rounds to zero. */
		D->d[0] = (want == 0 && (D->d[0] > '5' || (D->d[0] == '5' && D->nd > 1)))
		          ? '1' : '0';
		D->nd = 1;
		D->decexp = -prec;
	}
	pos = D->decexp + 1;
	if (pos <= 0) {
		buf[n++] = '0';
		if (prec > 0 || alt) buf[n++] = '.';
		for (i = 0; i < -pos && i < prec; i++) buf[n++] = '0';
		for (i = 0; i < prec + pos; i++) buf[n++] = i < D->nd ? D->d[i] : '0';
	} else {
		for (i = 0; i < pos; i++) buf[n++] = i < D->nd ? D->d[i] : '0';
		if (prec > 0 || alt) buf[n++] = '.';
		for (i = 0; i < prec; i++) buf[n++] = pos + i < D->nd ? D->d[pos + i] : '0';
	}
	return n;
}

/* %e-style body (no sign).  *epos receives the offset of the 'e', the
 * point at which emit_float splices in any zeros a clamped precision
 * left out of the mantissa. */
static int fmt_e(char *buf, struct dec *D, int prec, int alt, int upper, int *epos)
{
	int i, n = 0;

	dec_round(D, prec + 1);
	buf[n++] = D->d[0];
	if (prec > 0 || alt) {
		buf[n++] = '.';
		for (i = 1; i <= prec; i++) buf[n++] = i < D->nd ? D->d[i] : '0';
	}
	*epos = n;
	buf[n++] = upper ? 'E' : 'e';
	buf[n++] = D->decexp < 0 ? '-' : '+';
	{
		int ax = D->decexp < 0 ? -D->decexp : D->decexp;
		char eb[8]; int ei = 0;
		if (ax == 0) eb[ei++] = '0';
		while (ax) { eb[ei++] = (char)('0' + ax % 10); ax /= 10; }
		while (ei < 2) eb[ei++] = '0';
		while (ei--) buf[n++] = eb[ei];
	}
	return n;
}

/* strip trailing fractional zeros (and a bare trailing point) from a
 * body already formatted by fmt_f/fmt_e, for %g without '#'. */
static int strip_g(char *buf, int n, int has_exp)
{
	int mant_end = n, i;
	if (has_exp) { for (i = 0; i < n; i++) if (buf[i] == 'e' || buf[i] == 'E') { mant_end = i; break; } }
	i = mant_end;
	if (memchr(buf, '.', mant_end)) {
		while (i > 0 && buf[i - 1] == '0') i--;
		if (i > 0 && buf[i - 1] == '.') i--;
	}
	if (i != mant_end) memmove(buf + i, buf + mant_end, (size_t)(n - mant_end));
	return n - (mant_end - i);
}

/* %a-style body: the hex significand and the binary exponent in
 * decimal, without the sign and without the "0x" -- emit_float carries
 * those in the prefix, so that a '0' flag pads between them the way
 * C99 7.19.6.1p6 asks.  *epos receives the offset of the 'p', where
 * the zeros of a precision clamped to PREC_MAX belong.
 *
 * The 52 mantissa bits of a double are exactly 13 hex digits, so every
 * digit past the 13th is a zero whatever the value; a precision below
 * 13 rounds to nearest with ties to even, like the arithmetic itself. */
static int fmt_a(char *buf, double v, int prec, int alt, int upper, int *epos)
{
	const char *hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	union { double f; uint64_t i; } u;
	uint64_t man;
	int e, nd, i, n = 0;
	char lead;

	u.f = v;
	e = (int)(u.i >> 52 & 0x7ff);
	man = u.i & 0xfffffffffffffULL;
	if (!e) { lead = '0'; e = man ? -1022 : 0; }   /* subnormal, or zero */
	else { lead = '1'; e -= 1023; }

	if (prec < 0) {
		/* no precision given: exactly the digits the value needs */
		nd = 13;
		while (nd > 0 && !(man >> (52 - 4 * nd) & 0xf)) nd--;
	} else if (prec < 13) {
		int shift = (13 - prec) * 4;
		uint64_t rem = man & (((uint64_t)1 << shift) - 1);
		uint64_t half = (uint64_t)1 << (shift - 1);
		man >>= shift;
		/* a tie goes to even, where at a precision of 0 the digit
		 * that has to end up even is the leading one */
		if (rem > half || (rem == half && ((prec ? man : (uint64_t)(lead - '0')) & 1))) man++;
		if (man >> (4 * prec)) { man = 0; lead++; }   /* carried out of the digits */
		man <<= shift;
		nd = prec;
	} else nd = 13;   /* the caller's extra digits are zeros; see PREC_MAX */

	buf[n++] = lead;
	if (nd > 0 || alt) buf[n++] = '.';
	for (i = 0; i < nd; i++) buf[n++] = hex[man >> (48 - 4 * i) & 0xf];
	if (prec > 13) for (i = 13; i < prec; i++) buf[n++] = '0';
	*epos = n;
	buf[n++] = upper ? 'P' : 'p';
	buf[n++] = e < 0 ? '-' : '+';
	{
		int ax = e < 0 ? -e : e;
		char eb[8]; int ei = 0;
		if (ax == 0) eb[ei++] = '0';
		while (ax) { eb[ei++] = (char)('0' + ax % 10); ax /= 10; }
		while (ei--) buf[n++] = eb[ei];
	}
	return n;
}

/* Write a body of n bytes with `zeros` further '0' spliced in at offset
 * zpos (the end of the mantissa), which is where a precision clamped to
 * PREC_MAX left off. */
static void out_body(FILE *f, const char *body, int n, int zpos, long zeros, long *count, int *bad)
{
	out(f, body, (size_t)zpos, count, bad);
	pad(f, '0', (size_t)zeros, count, bad);
	out(f, body + zpos, (size_t)(n - zpos), count, bad);
}

static void emit_float(FILE *f, double v, int conv, int prec, int alt, int flags, int width, long *count, int *bad)
{
	char body[BODYMAX];
	struct dec D;
	char pfx[3];
	int n, neg = signbit(v);
	int upper = conv == 'F' || conv == 'E' || conv == 'G' || conv == 'A';
	char sign = neg ? '-' : (flags & 1 ? '+' : (flags & 2 ? ' ' : 0));
	char av = (char)(conv == 'F' ? 'f' : conv == 'E' ? 'e' : conv == 'G' ? 'g' :
	                 conv == 'A' ? 'a' : conv);
	int prefixlen = 0;
	long zeros = 0;   /* mantissa places past PREC_MAX, all of them zeros */
	int zpos = 0;     /* where in body they belong */
	int total, special = 0;

	v = fabs(v);
	/* A NaN keeps whatever sign the flags ask for: C99 7.19.6.1p6 says a
	 * signed conversion under '+' always begins with a sign, and p8's
	 * "[-]nan" only makes the minus conditional.  glibc and musl both
	 * print "+nan". */
	/* body[] is a length-tracked buffer (n), never a NUL-terminated C
	 * string -- out_body below always writes exactly n bytes. */
	if (isnan(v)) { memcpy(body, upper ? "NAN" : "nan", 3); n = 3; special = 1; } // NOLINT(bugprone-not-null-terminated-result)
	else if (isinf(v)) { memcpy(body, upper ? "INF" : "inf", 3); n = 3; special = 1; } // NOLINT(bugprone-not-null-terminated-result)
	else if (av == 'a') {
		int pu = prec > PREC_MAX ? PREC_MAX : prec;
		zeros = prec > PREC_MAX ? (long)prec - pu : 0;
		n = fmt_a(body, v, pu, alt, upper, &zpos);
	}
	else if (av == 'f' || av == 'e') {
		int p = prec < 0 ? 6 : prec;
		int pu = p > PREC_MAX ? PREC_MAX : p;
		zeros = (long)p - pu;
		dec_exact(v, &D);
		if (av == 'f') { n = fmt_f(body, &D, pu, alt); zpos = n; }
		else n = fmt_e(body, &D, pu, alt, upper, &zpos);
	} else { /* g/G */
		int P = prec < 0 ? 6 : (prec == 0 ? 1 : prec);
		int PU = P > PREC_MAX ? PREC_MAX : P;
		dec_exact(v, &D);
		/* the form is chosen from the rounded value's exponent (C99
		 * 7.19.6.1p8), so round first; fmt_e/fmt_f then round to the
		 * same width again, which is a no-op */
		dec_round(&D, PU);
		/* without '#' the zeros a clamped precision drops are exactly
		 * the ones strip_g would take off again, so they never go out */
		zeros = alt ? (long)P - PU : 0;
		/* decexp is at most 308, so the form is the same whether the
		 * unclamped or the clamped precision picks it */
		if (D.decexp < -4 || D.decexp >= P) {
			n = fmt_e(body, &D, PU - 1, alt, upper, &zpos);
			if (!alt) n = strip_g(body, n, 1);
		} else {
			n = fmt_f(body, &D, PU - 1 - D.decexp, alt);
			zpos = n;
			if (!alt) n = strip_g(body, n, 0);
		}
	}
	if (zeros <= 0 || zpos > n) { zeros = 0; zpos = n; }

	if (sign) pfx[prefixlen++] = sign;
	if (av == 'a' && !special) { pfx[prefixlen++] = '0'; pfx[prefixlen++] = upper ? 'X' : 'x'; }

	/* C99 7.19.6.1p3: the count printf returns is an int, so refuse a
	 * conversion whose zero run alone would not fit in one rather than
	 * spend an age emitting a result that cannot be reported. */
	if (zeros > (long)(INT_MAX - n - prefixlen)) {
		errno = EOVERFLOW;
		f->err = 1;
		*bad = 1;
		return;
	}
	total = n + (int)zeros + prefixlen;

	{
		int pad_n = width - total;
		if (pad_n < 0) pad_n = 0;
		if (flags & 4) { /* left */
			out(f, pfx, (size_t)prefixlen, count, bad);
			out_body(f, body, n, zpos, zeros, count, bad);
			pad(f, ' ', (size_t)pad_n, count, bad);
		} else if ((flags & 8) && !special) { /* zero pad, never for inf/nan */
			out(f, pfx, (size_t)prefixlen, count, bad);
			pad(f, '0', (size_t)pad_n, count, bad);
			out_body(f, body, n, zpos, zeros, count, bad);
		} else {
			pad(f, ' ', (size_t)pad_n, count, bad);
			out(f, pfx, (size_t)prefixlen, count, bad);
			out_body(f, body, n, zpos, zeros, count, bad);
		}
	}
}

int __vfprintf(FILE *f, const char *fmt, va_list ap)
{
	long count = 0;
	int bad = 0;
	const char *p = fmt;

	while (*p && !bad) {
		if (*p != '%') {
			const char *start = p;
			while (*p && *p != '%') p++;
			out(f, start, (size_t)(p - start), &count, &bad);
			continue;
		}
		p++;
		if (*p == '%') { out(f, "%", 1, &count, &bad); p++; continue; }

		{
			int flags = 0; /* 1=+ 2=space 4=- 8=0 16=# 32=' */
			int width = 0, prec = -1, haswidth = 0;
			int lm = LM_NONE;
			int neg_width = 0;

			for (;; p++) {
				if (*p == '-') flags |= 4;
				else if (*p == '+') flags |= 1;
				else if (*p == ' ') flags |= 2;
				else if (*p == '0') flags |= 8;
				else if (*p == '#') flags |= 16;
				/* fprintf.html's flag table: "'  [CX] (The
				 * <apostrophe>.)  The integer portion of the result
				 * of a decimal conversion ( %i, %d, %u, %f, %F, %g,
				 * or %G ) shall be formatted with thousands'
				 * grouping characters. ... The non-monetary grouping
				 * character is used."
				 *
				 * A [CX] flag, i.e. base POSIX rather than XSI, so
				 * it must be ACCEPTED whatever the locale.  Accepted
				 * and then ignored is not a stub here, it is the
				 * complete implementation: the grouping to apply is
				 * LC_NUMERIC's `grouping`, which is "" in the POSIX
				 * locale (src/misc/locale.c), and the POSIX locale
				 * is the only one this library has -- setlocale()
				 * accepts nothing else.  An empty grouping
				 * specification means no separators, so the flagged
				 * conversion must produce byte-for-byte what the
				 * unflagged one produces, which is what ignoring it
				 * does.  The bit is recorded rather than dropped so
				 * that a future locale with real grouping has
				 * somewhere to hook on.
				 *
				 * Leaving it out of this loop was NOT a cosmetic
				 * defect.  The apostrophe ended the flag scan, fell
				 * through the conversion switch's default arm, and
				 * that arm emits the bytes literally WITHOUT
				 * consuming an argument -- so every conversion after
				 * a %' in the same format read the previous one's
				 * argument.  printf("%'d %s\n", total, name) handed
				 * `total` to %s to dereference as a char *.  And in
				 * the POSIX locale the correct output for %'d is
				 * identical to %d, so the one visible symptom was
				 * the least alarming one. */
				else if (*p == '\'') flags |= 32;
				else break;
			}
			if (*p == '*') { width = va_arg(ap, int); p++; haswidth = 1; if (width < 0) { neg_width = 1; width = -width; } }
			else while (*p >= '0' && *p <= '9') { width = width * 10 + (*p++ - '0'); haswidth = 1; }
			if (neg_width) flags |= 4;
			(void)haswidth;
			if (*p == '.') {
				p++;
				if (*p == '*') { prec = va_arg(ap, int); p++; }
				else { prec = 0; while (*p >= '0' && *p <= '9') prec = prec * 10 + (*p++ - '0'); }
				if (prec < 0) prec = -1;
			}
			for (;;) {
				if (*p == 'h') { lm = lm == LM_h ? LM_hh : LM_h; p++; }
				else if (*p == 'l') { lm = lm == LM_l ? LM_ll : LM_l; p++; }
				else if (*p == 'j') { lm = LM_j; p++; }
				else if (*p == 'z') { lm = LM_z; p++; }
				else if (*p == 't') { lm = LM_t; p++; }
				else if (*p == 'L') { lm = LM_L; p++; }
				else break;
			}

			switch (*p) {
			case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': {
				int base = *p == 'o' ? 8 : (*p == 'x' || *p == 'X') ? 16 : 10;
				int upper = *p == 'X';
				int issigned = *p == 'd' || *p == 'i';
				int neg = 0;
				unsigned long long uv;
				char digbuf[32]; int dn = 0, zpad;
				char prefix[3]; int pn = 0;

				if (issigned) {
					long long sv;
					switch (lm) {
					case LM_hh: sv = (signed char)va_arg(ap, int); break; // NOLINT(cert-str34-c) -- deliberate sign extension of a %hhd argument, not a table index
					case LM_h: sv = (short)va_arg(ap, int); break;
					case LM_l: sv = va_arg(ap, long); break;
					case LM_ll: case LM_j: sv = va_arg(ap, long long); break;
					/* LLP64: long is 32 bits here while size_t and
					 * ptrdiff_t are 64, so `long` is simply the wrong
					 * type to pull these with -- "%zd" of a value above
					 * 4G printed its low half.  fprintf.html: z
					 * "applies to a size_t or the corresponding signed
					 * integer type argument", t likewise for ptrdiff_t.
					 * Pull each as the type the page names.
					 * src/stdio/scanf.c implements the same grammar and
					 * has always done this correctly; printf.c was the
					 * only offender. */
					case LM_z: sv = (long long)va_arg(ap, ssize_t); break;
					case LM_t: sv = (long long)va_arg(ap, ptrdiff_t); break;
					default: sv = va_arg(ap, int); break;
					}
					neg = sv < 0;
					/* Negate after widening, not before: -sv is
					 * undefined for LLONG_MIN, whose magnitude is not
					 * representable as a long long.  Converting first
					 * and negating the unsigned value is well defined
					 * (C99 6.3.1.3p2: it wraps modulo 2**64, which is
					 * exactly the magnitude wanted). */
					uv = neg ? __neg_mag((unsigned long long)sv) : (unsigned long long)sv;
				} else {
					switch (lm) {
					case LM_hh: uv = (unsigned char)va_arg(ap, unsigned int); break;
					case LM_h: uv = (unsigned short)va_arg(ap, unsigned int); break;
					case LM_l: uv = va_arg(ap, unsigned long); break;
					case LM_ll: case LM_j: uv = va_arg(ap, unsigned long long); break;
					case LM_z: uv = (unsigned long long)va_arg(ap, size_t); break;
					case LM_t: uv = (unsigned long long)va_arg(ap, ptrdiff_t); break;
					default: uv = va_arg(ap, unsigned int); break;
					}
				}

				if (uv == 0 && prec == 0) { /* "" for 0 with explicit precision 0 */ }
				else {
					unsigned long long t = uv;
					do { digbuf[dn++] = "0123456789abcdef"[t % (unsigned)base] ; if (upper && digbuf[dn-1] > '9') digbuf[dn-1] -= 32; t /= (unsigned)base; } while (t);
				}
				/* A precision is a minimum digit count with no upper
				 * bound (C99 7.19.6.1p5), so the leading zeros it
				 * calls for are padded out to the stream rather than
				 * stored: digbuf holds only the digits a value can
				 * actually have. */
				zpad = prec > dn ? prec - dn : 0;

				if (neg) prefix[pn++] = '-';
				else if (issigned && (flags & 1)) prefix[pn++] = '+';
				else if (issigned && (flags & 2)) prefix[pn++] = ' ';
				/* '#' octal needs a leading zero only if the precision
				 * has not already put one there */
				if ((flags & 16) && base == 8 && !zpad && (dn == 0 || digbuf[dn-1] != '0')) digbuf[dn++] = '0';
				if ((flags & 16) && base == 16 && uv != 0) { prefix[pn++] = '0'; prefix[pn++] = upper ? 'X' : 'x'; }

				if (zpad > INT_MAX - dn - pn) { errno = EOVERFLOW; f->err = 1; bad = 1; break; }
				{
					int total = dn + pn + zpad;
					int padn = width - total; if (padn < 0) padn = 0;
					int zero = (flags & 8) && !(flags & 4) && prec < 0;
					if (flags & 4) {
						out(f, prefix, (size_t)pn, &count, &bad);
						pad(f, '0', (size_t)zpad, &count, &bad);
						{ char rev[sizeof digbuf]; int i; for (i = 0; i < dn; i++) rev[i] = digbuf[dn - 1 - i]; out(f, rev, (size_t)dn, &count, &bad); }
						pad(f, ' ', (size_t)padn, &count, &bad);
					} else if (zero) {
						out(f, prefix, (size_t)pn, &count, &bad);
						pad(f, '0', (size_t)padn, &count, &bad);
						{ char rev[sizeof digbuf]; int i; for (i = 0; i < dn; i++) rev[i] = digbuf[dn - 1 - i]; out(f, rev, (size_t)dn, &count, &bad); }
					} else {
						pad(f, ' ', (size_t)padn, &count, &bad);
						out(f, prefix, (size_t)pn, &count, &bad);
						pad(f, '0', (size_t)zpad, &count, &bad);
						{ char rev[sizeof digbuf]; int i; for (i = 0; i < dn; i++) rev[i] = digbuf[dn - 1 - i]; out(f, rev, (size_t)dn, &count, &bad); }
					}
				}
				break;
			}
			case 'c': {
				char c = (char)va_arg(ap, int);
				int padn = width - 1; if (padn < 0) padn = 0;
				if (flags & 4) { out(f, &c, 1, &count, &bad); pad(f, ' ', (size_t)padn, &count, &bad); }
				else { pad(f, ' ', (size_t)padn, &count, &bad); out(f, &c, 1, &count, &bad); }
				break;
			}
			case 's': {
				const char *s = va_arg(ap, const char *);
				size_t n;
				int padn;
				if (!s) s = "(null)";
				n = strlen(s);
				if (prec >= 0 && (size_t)prec < n) n = (size_t)prec;
				padn = width - (int)n; if (padn < 0) padn = 0;
				if (flags & 4) { out(f, s, n, &count, &bad); pad(f, ' ', (size_t)padn, &count, &bad); }
				else { pad(f, ' ', (size_t)padn, &count, &bad); out(f, s, n, &count, &bad); }
				break;
			}
			case 'p': {
				void *ptr = va_arg(ap, void *);
				uintptr_t uv = (uintptr_t)ptr;
				int dn = 2;   /* the "0x" prefix, emitted literally below */
				if (!ptr) { out(f, "(nil)", 5, &count, &bad); break; }
				{
					char rev[sizeof(uintptr_t) * 2]; int rn = 0;
					do { rev[rn++] = "0123456789abcdef"[uv % 16]; uv /= 16; } while (uv);
					{
						int padn = width - (dn + rn); if (padn < 0) padn = 0;
						if (flags & 4) {
							out(f, "0x", 2, &count, &bad);
							{ char b2[sizeof(uintptr_t)*2]; int i; for (i=0;i<rn;i++) b2[i]=rev[rn-1-i]; out(f,b2,(size_t)rn,&count,&bad); }
							pad(f, ' ', (size_t)padn, &count, &bad);
						} else {
							pad(f, ' ', (size_t)padn, &count, &bad);
							out(f, "0x", 2, &count, &bad);
							{ char b2[sizeof(uintptr_t)*2]; int i; for (i=0;i<rn;i++) b2[i]=rev[rn-1-i]; out(f,b2,(size_t)rn,&count,&bad); }
						}
					}
				}
				break;
			}
			case 'n': {
				void *ptr = va_arg(ap, void *);
				switch (lm) {
				case LM_hh: *(signed char *)ptr = (signed char)count; break;
				case LM_h: *(short *)ptr = (short)count; break;
				case LM_l: *(long *)ptr = (long)count; break;
				case LM_ll: case LM_j: *(long long *)ptr = (long long)count; break;
				/* The worst of the three: this stored through
				 * *(long *), writing FOUR bytes into the caller's
				 * EIGHT-byte size_t and leaving the upper four whatever
				 * they happened to be -- silent corruption of an object
				 * the caller owns, not merely a wrong number printed. */
				case LM_z: *(size_t *)ptr = (size_t)count; break;
				case LM_t: *(ptrdiff_t *)ptr = (ptrdiff_t)count; break;
				default: *(int *)ptr = (int)count; break;
				}
				break;
			}
			case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
			case 'a': case 'A': {
				double v = va_arg(ap, double);
				emit_float(f, v, *p, prec, flags & 16, flags, width, &count, &bad);
				break;
			}
			default:
				/* an unknown conversion: emit it literally, the way glibc does */
				if (*p) { out(f, "%", 1, &count, &bad); out(f, p, 1, &count, &bad); }
				break;
			}
			if (*p) p++;
		}
	}
	return bad ? -1 : (int)count;
}

/* sprintf/snprintf/vsprintf/vsnprintf share this: a throwaway FILE that
 * is a fixed (or, for plain sprintf, unbounded) memory buffer exactly
 * as __file_write already knows how to fill. */
static int vxprintf_mem(char *s, size_t cap, const char *fmt, va_list ap)
{
	FILE mf;
	int r;
	memset(&mf, 0, sizeof mf);
	mf.fd = -1;
	mf.pid = -1;
	mf.is_mem = 1;
	mf.writable = 1;
	mf.bufmode = _IONBF;   /* every write must land in mem_buf right away: nothing ever flushes this FILE */
	mf.mem_buf = (unsigned char *)s;
	mf.mem_size = cap;
	r = __vfprintf(&mf, fmt, ap);
	/* __fwrite gives even an unbuffered memory FILE a one-byte staging
	 * buffer through __ensure_buf, and this FILE is a local that never
	 * sees fclose, so releasing it is ours to do -- otherwise every
	 * sprintf/snprintf that writes anything leaks that byte.  Same
	 * ownership rule as vsscanf_impl in scanf.c. */
	free(mf.buf);
	if (cap) {
		size_t pos = mf.mem_len;
		if (pos >= cap) pos = cap - 1;
		s[pos] = 0;
	}
	return r;
}

int vsprintf(char *__restrict s, const char *__restrict fmt, __isoc_va_list ap)
{
	return vxprintf_mem(s, (size_t)-1, fmt, ap);
}
int vsnprintf(char *__restrict s, size_t n, const char *__restrict fmt, __isoc_va_list ap)
{
	return vxprintf_mem(s, n, fmt, ap);
}
int vfprintf(FILE *__restrict f, const char *__restrict fmt, __isoc_va_list ap)
{
	return __vfprintf(f, fmt, ap);
}
int vprintf(const char *__restrict fmt, __isoc_va_list ap)
{
	return __vfprintf(stdout, fmt, ap);
}

int sprintf(char *__restrict s, const char *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = vsprintf(s, fmt, ap);
	va_end(ap);
	return r;
}
int snprintf(char *__restrict s, size_t n, const char *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = vsnprintf(s, n, fmt, ap);
	va_end(ap);
	return r;
}
int printf(const char *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = __vfprintf(stdout, fmt, ap);
	va_end(ap);
	return r;
}
int fprintf(FILE *__restrict f, const char *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = __vfprintf(f, fmt, ap);
	va_end(ap);
	return r;
}

int vdprintf(int fd, const char *__restrict fmt, __isoc_va_list ap)
{
	/* No FILE exists for fd; wrap it in one just for the call, the way
	 * fdopen would, but without touching the descriptor table. */
	FILE f;
	int r;
	memset(&f, 0, sizeof f);
	f.fd = fd;
	f.pid = -1;
	f.writable = 1;
	f.bufmode = _IONBF;
	r = __vfprintf(&f, fmt, ap);
	fflush(&f);
	/* __ensure_buf() allocated f.buf on the first write (one byte, for
	 * _IONBF) and nothing else will ever free it: this FILE is a stack
	 * object that never reaches fclose(), and fflush() only drains the
	 * buffer, it does not release it.  Without this, every dprintf()/
	 * vdprintf() call leaks -- caught by ASan under tools/asan-build.sh
	 * once test/posix-stdio.c started calling them at all.  Guarded on
	 * user_buf the same way __fclose_locked() and setvbuf() are, even
	 * though nothing can have handed this FILE a user buffer, so the
	 * ownership rule stays stated in one form everywhere. */
	if (f.buf && !f.user_buf) free(f.buf);
	return r;
}
int dprintf(int fd, const char *__restrict fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = vdprintf(fd, fmt, ap);
	va_end(ap);
	return r;
}

int vasprintf(char **s, const char *fmt, __isoc_va_list ap)
{
	va_list ap2;
	int n;
	va_copy(ap2, ap);
	n = vxprintf_mem(0, 0, fmt, ap2);
	va_end(ap2);
	if (n < 0) { *s = 0; return -1; }
	*s = malloc((size_t)n + 1);
	if (!*s) return -1;
	return vxprintf_mem(*s, (size_t)n + 1, fmt, ap);
}
int asprintf(char **s, const char *fmt, ...)
{
	va_list ap; int r;
	va_start(ap, fmt);
	r = vasprintf(s, fmt, ap);
	va_end(ap);
	return r;
}
