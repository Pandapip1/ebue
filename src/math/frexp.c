/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>
#include <stdint.h>
#include "x87.h"

double frexp(double x, int *e)
{
	union { double f; uint64_t i; } u = { x };
	int ee = (int)(u.i >> 52 & 0x7ff);

	if (!ee) {
		if (x) {
			x = frexp(x * 0x1p64, e);
			*e -= 64;
		} else *e = 0;
		return x;
	}
	if (ee == 0x7ff) { *e = 0; return x; }
	*e = ee - 0x3fe;
	u.i &= 0x800fffffffffffffULL;
	u.i |= 0x3fe0000000000000ULL;
	return u.f;
}

float frexpf(float x, int *e)
{
	union { float f; uint32_t i; } u = { x };
	int ee = (int)(u.i >> 23 & 0xff);

	if (!ee) {
		if (x) {
			x = frexpf(x * 0x1p30f, e);
			*e -= 30;
		} else *e = 0;
		return x;
	}
	if (ee == 0xff) { *e = 0; return x; }
	*e = ee - 0x7e;
	u.i &= 0x807fffff;
	u.i |= 0x3f000000;
	return u.f;
}

/* ntlibc is built with two compilers with two different long double
 * formats - see the NTLIBC_LDBL_EXTENDED comment in src/math/x87.h.
 *
 * Under tcc, "long double" is really just "double" (8 bytes, no 80-bit
 * extended format), so this uses the same 64-bit layout as frexp()
 * above (reading the 80-bit/16-byte layout a native long double would
 * need used to read two bytes past the actual object, i.e.
 * uninitialized stack garbage).
 *
 * Under gcc/mingw, "long double" is the true 80-bit x87 extended
 * format, packed as a little-endian uint64_t explicit-integer-bit
 * mantissa followed by a uint16_t sign+exponent half (see
 * __fpclassifyl's comment in fpclassify.c).  The exponent bias there
 * is 0x3fff (not 0x3ff as for a 64-bit double), and because the
 * mantissa's integer bit is explicit (not implicit as in double), a
 * normalized "mantissa in [0.5, 1)" result is encoded by biasing the
 * exponent to 0x3ffe while leaving the mantissa bits themselves
 * untouched - matching musl's LDBL_MANT_DIG==64 frexpl(). */
long double frexpl(long double x, int *e)
{
#if NTLIBC_LDBL_EXTENDED
	union { long double f; struct { uint64_t m; uint16_t se; } i; } u = { x };
	int ee = u.i.se & 0x7fff;

	if (!ee) {
		if (x) {
			x = frexpl(x * 0x1p120L, e);
			*e -= 120;
		} else *e = 0;
		return x;
	}
	if (ee == 0x7fff) return x;
	*e = ee - 0x3ffe;
	u.i.se &= 0x8000;
	u.i.se |= 0x3ffe;
	return u.f;
#else
	union { long double f; uint64_t i; } u = { x };
	int ee = (int)(u.i >> 52 & 0x7ff);

	if (!ee) {
		if (x) {
			x = frexpl(x * 0x1p64L, e);
			*e -= 64;
		} else *e = 0;
		return x;
	}
	if (ee == 0x7ff) { *e = 0; return x; }
	*e = ee - 0x3fe;
	u.i &= 0x800fffffffffffffULL;
	u.i |= 0x3fe0000000000000ULL;
	return u.f;
#endif
}
