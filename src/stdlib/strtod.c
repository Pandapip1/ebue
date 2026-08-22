/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* strtod/strtof/strtold.
 *
 * On this target "long double" is just a double (see arch/x86_64's tcc:
 * it does not give the type an 80-bit range), so a scheme that leans on
 * long double having extra range or precision is not available here,
 * and neither is a double-double approximation of 10^e: rounding the
 * decimal mantissa, then the power, then the product accumulates well
 * past the one-ulp error C99 7.20.1.3p9 allows, and the intermediate
 * squarings overflow to infinity (and then to NaN) for exponents that
 * the final result would have saturated to HUGE_VAL on perfectly well.
 *
 * So the conversion is done exactly instead.  Both a decimal and a hex
 * input name a value N / D * 2^e2 for integers N and D, and the
 * correctly rounded result is obtained by long-dividing the two as
 * big integers, after scaling so the quotient has exactly as many bits
 * as the destination format's significand; the remainder then decides
 * the rounding, with ties broken to even.  That is a single rounding of
 * the exact value, which is what C99 7.20.1.3p9 asks for.
 *
 * Only the first MAXDIG significant digits are kept; anything after
 * them is folded into a sticky flag.  This is exact, not an
 * approximation: a rounding boundary (a midpoint between two adjacent
 * doubles, i.e. an odd multiple of 2^-1075 at worst) has at most 752
 * significant decimal digits, so it can never fall strictly inside the
 * one-unit-in-the-last-kept-place interval that the discarded tail
 * spans.  Either the boundary is exactly the kept prefix - and then the
 * sticky flag says the true value is above it, so we round up - or the
 * kept prefix already decides the rounding for the whole value. */
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <float.h>

/* Significant decimal digits kept exactly; see the note above for why
 * this bound makes the conversion exact rather than merely close. */
#define MAXDIG 800

/* Big non-negative integers, base 2^32, least significant limb first.
 * n is the number of limbs in use, with no leading zero limb, so n == 0
 * means the value is zero.  The size is chosen so that nothing here can
 * overflow it: the widest intermediate is the divisor scaled by the
 * quotient width, at most 800 digits (2658 bits) of decimal mantissa
 * plus 1074 bits of subnormal scaling plus 53 bits of quotient. */
#define BN_LIMBS 180
typedef struct { int n; uint32_t d[BN_LIMBS]; } bn_t;

/* Only the limbs in use are copied; the rest of d is stale, which
 * every operation here is careful never to look at. */
static void bn_copy(bn_t *dst, const bn_t *src)
{
	int i;
	dst->n = src->n;
	for (i = 0; i < src->n; i++) dst->d[i] = src->d[i];
}

static void bn_setu32(bn_t *a, uint32_t v)
{
	a->d[0] = v;
	a->n = v ? 1 : 0;
}

/* a = a * m + c */
static void bn_muladd(bn_t *a, uint32_t m, uint32_t c)
{
	uint64_t carry = c;
	int i;

	for (i = 0; i < a->n; i++) {
		uint64_t t = (uint64_t)a->d[i] * m + carry;
		a->d[i] = (uint32_t)t;
		carry = t >> 32;
	}
	while (carry && a->n < BN_LIMBS) {
		a->d[a->n++] = (uint32_t)carry;
		carry >>= 32;
	}
}

static int bn_bits(const bn_t *a)
{
	uint32_t v;
	int b;

	if (!a->n) return 0;
	v = a->d[a->n - 1];
	b = (a->n - 1) * 32;
	while (v) { b++; v >>= 1; }
	return b;
}

static void bn_shl(bn_t *a, int k)
{
	int w = k / 32, b = k % 32, i;

	if (!a->n || k <= 0) return;
	if (b) {
		uint32_t carry = 0;
		for (i = 0; i < a->n; i++) {
			uint32_t v = a->d[i];
			a->d[i] = (v << b) | carry;
			carry = v >> (32 - b);
		}
		if (carry && a->n < BN_LIMBS) a->d[a->n++] = carry;
	}
	if (w) {
		if (a->n + w > BN_LIMBS) w = BN_LIMBS - a->n; /* cannot happen; do not corrupt memory if it does */
		for (i = a->n - 1; i >= 0; i--) a->d[i + w] = a->d[i];
		for (i = 0; i < w; i++) a->d[i] = 0;
		a->n += w;
	}
}

static void bn_shr1(bn_t *a)
{
	int i;

	if (!a->n) return;
	for (i = 0; i < a->n - 1; i++)
		a->d[i] = (a->d[i] >> 1) | (a->d[i + 1] << 31);
	a->d[a->n - 1] >>= 1;
	if (!a->d[a->n - 1]) a->n--;
}

static int bn_cmp(const bn_t *a, const bn_t *b)
{
	int i;

	if (a->n != b->n) return a->n < b->n ? -1 : 1;
	for (i = a->n - 1; i >= 0; i--)
		if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
	return 0;
}

/* a -= b; the caller guarantees a >= b. */
static void bn_sub(bn_t *a, const bn_t *b)
{
	uint64_t borrow = 0;
	int i;

	for (i = 0; i < a->n; i++) {
		uint64_t t = (uint64_t)a->d[i] - (i < b->n ? b->d[i] : 0) - borrow;
		a->d[i] = (uint32_t)t;
		borrow = (t >> 32) & 1;
	}
	while (a->n && !a->d[a->n - 1]) a->n--;
}

/* a *= 10^e (e >= 0), as 5^e followed by an exact shift. */
static void bn_mul_pow10(bn_t *a, int e)
{
	int k = e;

	while (k >= 13) { bn_muladd(a, 1220703125u, 0); k -= 13; } /* 5^13 */
	if (k) {
		uint32_t m = 1;
		while (k--) m *= 5;
		bn_muladd(a, m, 0);
	}
	bn_shl(a, e);
}

/* The correctly rounded value of (N / D) * 2^e2, in a format with p
 * significand bits whose smallest exponent is emin (so the smallest
 * positive value is 2^emin).  sticky says the true value is strictly
 * greater than N / D * 2^e2 by less than one unit in its last place.
 * N must be nonzero and D must be nonzero.  The result is exact in
 * double even when p is float's 24, so a caller converting to float
 * afterwards does not round a second time. */
static double bn_scale_round(bn_t *N, bn_t *D, int e2, int sticky, int p, int emin)
{
	bn_t A, B, T;
	int k, g, t, i, c;
	uint64_t q = 0;

	k = bn_bits(N) - bn_bits(D) + e2; /* value is in (2^(k-1), 2^(k+1)) */
	if (k >= 1025) return HUGE_VAL;   /* past every finite format here */
	if (k <= emin - 2) return 0.0;    /* below half the smallest value */

	g = k - p;
	if (g < emin) g = emin;
	for (;;) {
		bn_copy(&A, N); bn_copy(&B, D);
		t = e2 - g;
		if (t > 0) bn_shl(&A, t); else bn_shl(&B, -t);
		/* want floor(A / B) to have exactly p bits, or fewer only
		 * when g has bottomed out at emin (a subnormal result). */
		bn_copy(&T, &B); bn_shl(&T, p);
		if (bn_cmp(&A, &T) >= 0) { g++; continue; }
		if (g > emin) {
			bn_copy(&T, &B); bn_shl(&T, p - 1);
			if (bn_cmp(&A, &T) < 0) { g--; continue; }
		}
		break;
	}

	/* Restoring long division: q = floor(A / B), A becomes the remainder. */
	bn_copy(&T, &B); bn_shl(&T, p - 1);
	for (i = p - 1; i >= 0; i--) {
		if (bn_cmp(&A, &T) >= 0) { bn_sub(&A, &T); q |= (uint64_t)1 << i; }
		bn_shr1(&T);
	}

	/* Round to nearest, ties to even; a sticky tail beats the tie. */
	bn_shl(&A, 1);
	c = bn_cmp(&A, &B);
	if (c > 0 || (c == 0 && (sticky || (q & 1)))) {
		q++;
		if (q >> p) { q >>= 1; g++; }
	}
	return scalbn((double)q, g);
}

static int ci_prefix(const char *s, const char *word)
{
	int n = 0;
	while (*word) {
		if (tolower((unsigned char)s[n]) != *word) return 0;
		n++; word++;
	}
	return n;
}

static double parse_hex(const char *s, const char **end, int *ok, int *nz, int p, int emin)
{
	bn_t N, D;
	uint64_t m = 0;
	int exp = 0, any = 0, seen_dot = 0, sticky = 0, e = 0, eneg = 0, d;
	const char *p2 = s;

	for (;; p2++) {
		if (isdigit((unsigned char)*p2)) d = *p2 - '0';
		else if (*p2 >= 'a' && *p2 <= 'f') d = *p2 - 'a' + 10;
		else if (*p2 >= 'A' && *p2 <= 'F') d = *p2 - 'A' + 10;
		else if (*p2 == '.' && !seen_dot) { seen_dot = 1; continue; }
		else break;
		any = 1;
		if (m >> 60) { sticky |= d; if (!seen_dot) exp += 4; }
		else { m = (m << 4) | (unsigned)d; if (seen_dot) exp -= 4; }
	}
	if (!any) { *ok = 0; return 0; }
	if (sticky) m |= 1; /* the tail is below every bit we kept */
	if (*p2 == 'p' || *p2 == 'P') {
		const char *q = p2 + 1;
		if (*q == '+') q++; else if (*q == '-') { eneg = 1; q++; }
		if (isdigit((unsigned char)*q)) {
			while (isdigit((unsigned char)*q)) { if (e < 100000) e = e * 10 + (*q - '0'); q++; }
			p2 = q;
		}
	}
	exp += eneg ? -e : e;
	*end = p2;
	*ok = 1;
	*nz = m != 0;
	if (!m) return 0.0;
	bn_setu32(&N, (uint32_t)m);
	if (m >> 32) { N.d[1] = (uint32_t)(m >> 32); N.n = 2; }
	bn_setu32(&D, 1);
	return bn_scale_round(&N, &D, exp, 0, p, emin);
}

static double parse_dec(const char *s, const char **end, int *ok, int *nz, int p, int emin)
{
	bn_t N, D;
	unsigned char dig[MAXDIG];
	int nd = 0, exp = 0, any = 0, seen_dot = 0, e = 0, eneg = 0, sticky = 0, i;
	const char *p2 = s;

	for (;; p2++) {
		if (isdigit((unsigned char)*p2)) {
			any = 1;
			if (nd < MAXDIG) {
				if (nd || *p2 != '0') dig[nd++] = (unsigned char)(*p2 - '0');
				if (seen_dot) exp--;
			} else {
				if (*p2 != '0') sticky = 1;
				if (!seen_dot) exp++;
			}
		} else if (*p2 == '.' && !seen_dot) {
			seen_dot = 1;
		} else break;
	}
	if (!any) { *ok = 0; return 0; }
	if (*p2 == 'e' || *p2 == 'E') {
		const char *q = p2 + 1;
		if (*q == '+') q++; else if (*q == '-') { eneg = 1; q++; }
		if (isdigit((unsigned char)*q)) {
			while (isdigit((unsigned char)*q)) { if (e < 100000) e = e * 10 + (*q - '0'); q++; }
			p2 = q;
		}
	}
	exp += eneg ? -e : e;
	*end = p2;
	*ok = 1;
	*nz = nd != 0;
	if (!nd) return 0.0;

	/* Trailing zeros of the kept prefix only make the big integers
	 * wider; the sticky tail's size is unaffected by dropping them. */
	while (nd > 1 && dig[nd - 1] == 0) { nd--; exp++; }

	/* The value lies in [10^(exp+nd-1), 10^(exp+nd)), which is enough
	 * to settle the two extremes without building anything: 10^309 is
	 * already past DBL_MAX and 10^-324 is below half the smallest
	 * subnormal.  This also keeps the shifts below from running away
	 * on an exponent like 1e99999. */
	if (exp + nd >= 310) return HUGE_VAL;
	if (exp + nd <= -324) return 0.0;

	bn_setu32(&N, 0);
	for (i = 0; i < nd; ) {
		uint32_t chunk = 0, mul = 1;
		int j;
		for (j = 0; j < 9 && i < nd; j++, i++) { chunk = chunk * 10 + dig[i]; mul *= 10; }
		bn_muladd(&N, mul, chunk);
	}
	bn_setu32(&D, 1);
	if (exp > 0) bn_mul_pow10(&N, exp);
	else if (exp < 0) bn_mul_pow10(&D, -exp);
	return bn_scale_round(&N, &D, 0, sticky, p, emin);
}

static long double strtox(const char *s0, char **endptr, int kind)
{
	const char *s = s0, *end;
	double v;
	int neg = 0, ok = 0, n, nz = 1, lit = 0;
	int p = kind == 0 ? 24 : 53;
	int emin = kind == 0 ? -149 : -1074;

	while (isspace((unsigned char)*s)) s++;
	if (*s == '+') s++; else if (*s == '-') { neg = 1; s++; }

	if ((n = ci_prefix(s, "inf"))) {
		int n2 = ci_prefix(s, "infinity");
		end = s + (n2 ? n2 : n);
		v = HUGE_VAL;
		ok = 1; lit = 1;
	} else if ((n = ci_prefix(s, "nan"))) {
		end = s + n;
		if (*end == '(') {
			const char *q = end + 1;
			while (isalnum((unsigned char)*q) || *q == '_') q++;
			if (*q == ')') end = q + 1;
		}
		v = NAN;
		ok = 1; lit = 1;
	} else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		v = parse_hex(s + 2, &end, &ok, &nz, p, emin);
		if (!ok) { /* just "0" then */
			v = 0; end = s + 1; ok = 1; nz = 0;
		}
	} else {
		v = parse_dec(s, &end, &ok, &nz, p, emin);
	}
	if (!ok) { if (endptr) *endptr = (char *)s0; return 0; }
	if (endptr) *endptr = (char *)end;

	/* Range check in the destination type.  v is already the exactly
	 * rounded value in that type's precision, so a plain comparison
	 * against its limits is the whole story: no second rounding can
	 * pull an out-of-range value back in, or push an in-range one out.
	 * An "inf" or "nan" spelling is not a range error at all - nothing
	 * was rounded there - so it skips this. */
	if (!lit) {
		double max = kind == 0 ? FLT_MAX : DBL_MAX;
		double min = kind == 0 ? FLT_MIN : DBL_MIN;
		if (v > max) { errno = ERANGE; v = HUGE_VAL; }
		else if (nz && v < min) errno = ERANGE;
	}
	return neg ? -(long double)v : (long double)v;
}

float strtof(const char *__restrict s, char **__restrict e) { return (float)strtox(s, e, 0); }
double strtod(const char *__restrict s, char **__restrict e) { return (double)strtox(s, e, 1); }
long double strtold(const char *__restrict s, char **__restrict e) { return strtox(s, e, 2); }
