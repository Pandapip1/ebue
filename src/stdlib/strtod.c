/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* strtod/strtof/strtold.
 *
 * Decimal input is accumulated as an integer mantissa of up to 19
 * significant digits in a uint64_t (further digits only adjust the
 * exponent, with a sticky bit so the rounding direction is right).
 *
 * On this target "long double" is just a double (see arch/x86_64's tcc:
 * it does not give the type an 80-bit range), so a scheme that leans on
 * long double having extra range or precision is not available here.
 * 10^e is instead split as 5^e * 2^e: 5^e has a far smaller dynamic
 * range than 10^e for the same e (about 0.7 decades per unit of e
 * instead of 1), so it stays a normal, finite double for every e that
 * can matter to a double result, whereas materialising 10^e itself
 * would overflow or flush to zero long before the final answer does.
 * 5^e is computed by binary powering in double-double (two doubles,
 * "dd" below) for near-double-double precision, then the exact power of
 * two is applied last via scalbn/fscale, which rounds correctly even
 * into subnormals.  Extreme exponents make 5^e itself overflow to
 * infinity or (via division) flush to zero, and scalbn of that carries
 * the same infinity/zero through to the result, which is the correct
 * saturation for a decimal exponent that extreme. */
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <float.h>

typedef struct { double hi, lo; } dd_t;

static dd_t dd_quick_two_sum(double a, double b)
{
	double s = a + b, e = b - (s - a);
	dd_t r; r.hi = s; r.lo = e; return r;
}

static void dd_split(double a, double *hi, double *lo)
{
	double t = a * 134217729.0; /* 2^27 + 1 */
	*hi = t - (t - a);
	*lo = a - *hi;
}

static dd_t dd_two_prod(double a, double b)
{
	double p = a * b, ahi, alo, bhi, blo, e;
	dd_split(a, &ahi, &alo);
	dd_split(b, &bhi, &blo);
	e = ((ahi * bhi - p) + ahi * blo + alo * bhi) + alo * blo;
	{ dd_t r; r.hi = p; r.lo = e; return r; }
}

/* dd * dd -> dd, good to about the last bit of the low double.  Once
 * either side has gone non-finite (a squaring step overflowed to
 * infinity), Dekker's split() would turn that into a NaN (inf - inf);
 * fall back to a plain multiply, which propagates infinity/zero the
 * way IEEE arithmetic should. */
static dd_t dd_mul(dd_t a, dd_t b)
{
	dd_t p;
	if (!isfinite(a.hi) || !isfinite(b.hi)) { dd_t r; r.hi = a.hi * b.hi; r.lo = 0; return r; }
	p = dd_two_prod(a.hi, b.hi);
	p.lo += a.hi * b.lo + a.lo * b.hi;
	return dd_quick_two_sum(p.hi, p.lo);
}

/* 5^|e| as a dd, computed by binary powering; the caller divides
 * instead of negating e so 5^e for negative e is never formed by
 * squaring past overflow and reciprocating (a range that, unlike
 * 10^e's, easily could pass through infinity on the way). */
static dd_t dd_pow5(unsigned n)
{
	dd_t r = { 1.0, 0.0 }, b = { 5.0, 0.0 };
	while (n) {
		if (n & 1) r = dd_mul(r, b);
		b = dd_mul(b, b);
		n >>= 1;
	}
	return r;
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

static long double parse_hex(const char *s, const char **end, int *ok, int *nz)
{
	uint64_t m = 0;
	int exp = 0, any = 0, seen_dot = 0, sticky = 0, e = 0, eneg = 0, d;
	const char *p = s;

	for (;; p++) {
		if (isdigit((unsigned char)*p)) d = *p - '0';
		else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
		else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
		else if (*p == '.' && !seen_dot) { seen_dot = 1; continue; }
		else break;
		any = 1;
		if (m >> 60) { sticky |= d; if (!seen_dot) exp += 4; }
		else { m = (m << 4) | (unsigned)d; if (seen_dot) exp -= 4; }
	}
	if (!any) { *ok = 0; return 0; }
	if (sticky) m |= 1;
	if (*p == 'p' || *p == 'P') {
		const char *q = p + 1;
		if (*q == '+') q++; else if (*q == '-') { eneg = 1; q++; }
		if (isdigit((unsigned char)*q)) {
			while (isdigit((unsigned char)*q)) { if (e < 100000) e = e * 10 + (*q - '0'); q++; }
			p = q;
		}
	}
	exp += eneg ? -e : e;
	*end = p;
	*ok = 1;
	*nz = m != 0;
	return ldexpl((long double)m, exp);
}

static long double parse_dec(const char *s, const char **end, int *ok, int *nz)
{
	uint64_t m = 0;
	int ndig = 0, exp = 0, any = 0, seen_dot = 0, e = 0, eneg = 0, sticky = 0;
	const char *p = s;

	for (;; p++) {
		if (isdigit((unsigned char)*p)) {
			any = 1;
			if (ndig < 19) {
				if (m || *p != '0') { m = m * 10 + (unsigned)(*p - '0'); ndig++; }
				if (seen_dot) exp--;
			} else {
				if (*p != '0') sticky = 1;
				if (!seen_dot) exp++;
			}
		} else if (*p == '.' && !seen_dot) {
			seen_dot = 1;
		} else break;
	}
	if (!any) { *ok = 0; return 0; }
	if (*p == 'e' || *p == 'E') {
		const char *q = p + 1;
		if (*q == '+') q++; else if (*q == '-') { eneg = 1; q++; }
		if (isdigit((unsigned char)*q)) {
			while (isdigit((unsigned char)*q)) { if (e < 100000) e = e * 10 + (*q - '0'); q++; }
			p = q;
		}
	}
	exp += eneg ? -e : e;
	*end = p;
	*ok = 1;
	*nz = m != 0;
	if (!m) return 0.0L;
	{
		double v = (double)m, p5d;
		dd_t p5;
		if (sticky) v += 0.5;   /* below the last kept digit; keeps rounding direction */
		p5 = dd_pow5(exp < 0 ? (unsigned)-exp : (unsigned)exp);
		p5d = p5.hi + p5.lo;
		v = exp < 0 ? v / p5d : v * p5d;
		/* the 2^exp half of 10^exp = 5^exp * 2^exp; scalbn (fscale) applies
		 * it exactly, rounding correctly even into subnormals, and turns
		 * an already-infinite/zero v (5^exp itself saturated) into the
		 * same infinity/zero, which is the right answer for an exponent
		 * that extreme regardless of the mantissa. */
		return (long double)scalbn(v, exp);
	}
}

static long double strtox(const char *s0, char **endptr, int kind)
{
	const char *s = s0, *end;
	long double v;
	int neg = 0, ok = 0, n, nz = 1;

	while (isspace((unsigned char)*s)) s++;
	if (*s == '+') s++; else if (*s == '-') { neg = 1; s++; }

	if ((n = ci_prefix(s, "inf"))) {
		int n2 = ci_prefix(s, "infinity");
		end = s + (n2 ? n2 : n);
		v = HUGE_VALL;
		ok = 1;
	} else if ((n = ci_prefix(s, "nan"))) {
		end = s + n;
		if (*end == '(') {
			const char *q = end + 1;
			while (isalnum((unsigned char)*q) || *q == '_') q++;
			if (*q == ')') end = q + 1;
		}
		v = NAN;
		ok = 1;
	} else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		v = parse_hex(s + 2, &end, &ok, &nz);
		if (!ok) { /* just "0" then */
			v = 0; end = s + 1; ok = 1; nz = 0;
		}
	} else {
		v = parse_dec(s, &end, &ok, &nz);
	}
	if (!ok) { if (endptr) *endptr = (char *)s0; return 0; }
	if (endptr) *endptr = (char *)end;

	/* Range check in the destination type. */
	if (v != v) return neg ? -v : v;
	{
		long double max = kind == 0 ? FLT_MAX : kind == 1 ? DBL_MAX : LDBL_MAX;
		long double min = kind == 0 ? FLT_MIN : kind == 1 ? DBL_MIN : LDBL_MIN;
		if (v > max) {
			/* round to the type first: a value just above max that
			 * rounds down is not an overflow. */
			long double r = kind == 0 ? (long double)(float)v : kind == 1 ? (long double)(double)v : v;
			if (r > max) { errno = ERANGE; v = HUGE_VALL; }
		} else if (v == 0) {
			/* a nonzero decimal/hex value that rounded all the way down
			 * to zero is an underflow just as much as one that only
			 * made it to a tiny nonzero value below min. */
			if (nz) errno = ERANGE;
		} else if (v < min) {
			long double r = kind == 0 ? (long double)(float)v : kind == 1 ? (long double)(double)v : v;
			if (r == 0 || r < min) errno = ERANGE;
		}
	}
	return neg ? -v : v;
}

float strtof(const char *__restrict s, char **__restrict e) { return (float)strtox(s, e, 0); }
double strtod(const char *__restrict s, char **__restrict e) { return (double)strtox(s, e, 1); }
long double strtold(const char *__restrict s, char **__restrict e) { return strtox(s, e, 2); }
