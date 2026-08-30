/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* nextafter.html/nexttoward.html: the adjacent representable value of
 * x towards y. Implemented via integer bit-twiddling on the IEEE
 * representation, the same style as src/math/copysign.c/fabs.c: for a
 * nonzero finite x, the raw sign-magnitude bit pattern, read as an
 * unsigned integer with the sign bit masked off, is monotonic in the
 * magnitude of x -- so "one step towards +-infinity" is exactly "the
 * magnitude bits +-1", independent of exponent/mantissa boundaries
 * (each of which the +-1 carries/borrows through correctly, including
 * normal<->subnormal). That holds for the formats whose leading
 * significand bit is implicit -- float and double here; x87 extended
 * spells that bit out, which breaks the monotonicity and is handled
 * explicitly in nextafterl. x==0 is handled separately (there is no "old
 * magnitude" to step from); the result is then classified by comparing
 * the old and new exponent fields for the overflow-to-infinity and
 * underflow-to-subnormal range-error cases. */
#include <math.h>
#include <fenv.h>
#include <stdint.h>
#include "ldbl_math.h"

double nextafter(double x, double y)
{
	union { double f; uint64_t i; } ux = { x }, uy = { y };
	uint64_t ai;
	unsigned oldexp, newexp;

	if (x != x || y != y) return x + y;
	if (x == y) return y;

	if (x == 0.0) {
		ux.i = (uy.i & ((uint64_t)1 << 63)) | (uint64_t)1;
		feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
		return ux.f;
	}

	oldexp = (unsigned)(ux.i >> 52 & 0x7ff);
	if ((x > 0.0) ? (y > x) : (y < x)) ai = ux.i + 1;
	else ai = ux.i - 1;
	newexp = (unsigned)(ai >> 52 & 0x7ff);

	if (newexp == 0x7ff) feraiseexcept(FE_OVERFLOW | FE_INEXACT);
	else if (newexp == 0 && oldexp != 0) feraiseexcept(FE_UNDERFLOW | FE_INEXACT);

	ux.i = ai;
	return ux.f;
}

float nextafterf(float x, float y)
{
	union { float f; uint32_t i; } ux = { x }, uy = { y };
	uint32_t ai;
	unsigned oldexp, newexp;

	if (x != x || y != y) return x + y;
	if (x == y) return y;

	if (x == 0.0f) {
		ux.i = (uy.i & 0x80000000u) | 1u;
		feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
		return ux.f;
	}

	oldexp = (ux.i >> 23) & 0xff;
	if ((x > 0.0f) ? (y > x) : (y < x)) ai = ux.i + 1;
	else ai = ux.i - 1;
	newexp = (ai >> 23) & 0xff;

	if (newexp == 0xff) feraiseexcept(FE_OVERFLOW | FE_INEXACT);
	else if (newexp == 0 && oldexp != 0) feraiseexcept(FE_UNDERFLOW | FE_INEXACT);

	ux.i = ai;
	return ux.f;
}

/* ntlibc is built with two compilers with two different long double
 * formats - see the NTLIBC_LDBL_EXTENDED comment in src/math/ldbl_math.h.
 *
 * Under tcc, "long double" is really just "double" (8 bytes), so this
 * is the same 64-bit layout as nextafter() above.
 *
 * Under gcc/mingw, "long double" is the true 80-bit x87 extended
 * format: a 64-bit explicit mantissa followed by a 16-bit sign+
 * exponent half (see __fpclassifyl's comment in fpclassify.c). The
 * step is not a plain +-1 on the pair. Unlike float and double, the
 * 80-bit format states its leading significand bit rather than
 * implying it, so a normal number's mantissa runs over
 * [2^63, 2^64) and never below: the pattern read as one wide integer
 * is not monotonic in magnitude across an exponent boundary, and the
 * step has to put the mantissa back at the right end of that range
 * itself. Going up, the successor of (e, 2^64-1) is (e+1, 2^63);
 * going down, the predecessor of (e, 2^63) is (e-1, 2^64-1). The one
 * asymmetry is at the bottom: e == 0 denotes the subnormals, which
 * share e == 1's scale, so the predecessor of (1, 2^63) is
 * (0, 2^63-1) -- exponent down, mantissa not wrapped -- and going the
 * other way (0, 2^63-1)'s successor is (1, 2^63).
 *
 * Both directions collapse to a test on the mantissa's low 63 bits
 * (LDBL_M_LOW63 below), which are all set at exactly the two mantissas
 * that have no successor at their own exponent -- 2^64-1, and the
 * largest subnormal 2^63-1 -- and all clear at exactly the one that
 * has no predecessor at its own exponent, the least normal 2^63 (m ==
 * 0 is zero, which x == 0 has already taken). So stepping up from
 * either of the first two gives (e+1, 2^63), and stepping down from
 * the third gives (e-1, 2^64-1), or (0, 2^63-1) when that new exponent
 * is 0 and the subnormal end is what is wanted. Everything else is
 * m+-1 at a fixed exponent. It is done this way rather than by
 * treating the pair as one wide integer because tcc has no 96-bit
 * integer type to do that with, and it would not be portable
 * regardless. */
#define LDBL_M_LOW63 ((((uint64_t)1 << 63) - 1))

long double nextafterl(long double x, long double y)
{
#if NTLIBC_LDBL_EXTENDED
	union { long double f; struct { uint64_t m; uint16_t se; } i; } ux = { x }, uy = { y };
	unsigned oldexp, newexp, e;
	int away;

	if (x != x || y != y) return x + y;
	if (x == y) return y;

	if (x == 0.0L) {
		ux.i.se = (uint16_t)(uy.i.se & 0x8000);
		ux.i.m = 1;
		feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
		return ux.f;
	}

	oldexp = ux.i.se & 0x7fff;
	away = (x > 0.0L) ? (y > x) : (y < x);
	if (away) {
		if ((ux.i.m & LDBL_M_LOW63) == LDBL_M_LOW63) {
			e = (unsigned)(oldexp + 1) & 0x7fff;
			ux.i.se = (uint16_t)((ux.i.se & 0x8000) | e);
			ux.i.m = (uint64_t)1 << 63;
		} else {
			ux.i.m++;
		}
	} else {
		if ((ux.i.m & LDBL_M_LOW63) == 0) {
			e = (unsigned)(oldexp - 1) & 0x7fff;
			ux.i.se = (uint16_t)((ux.i.se & 0x8000) | e);
			ux.i.m = e ? (uint64_t)-1 : LDBL_M_LOW63;
		} else {
			ux.i.m--;
		}
	}
	newexp = ux.i.se & 0x7fff;

	if (newexp == 0x7fff) feraiseexcept(FE_OVERFLOW | FE_INEXACT);
	else if (newexp == 0 && oldexp != 0) feraiseexcept(FE_UNDERFLOW | FE_INEXACT);

	return ux.f;
#else
	union { long double f; uint64_t i; } ux = { x }, uy = { y };
	uint64_t ai;
	unsigned oldexp, newexp;

	if (x != x || y != y) return x + y;
	if (x == y) return y;

	if (x == 0.0L) {
		ux.i = (uy.i & ((uint64_t)1 << 63)) | (uint64_t)1;
		feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
		return ux.f;
	}

	oldexp = (unsigned)(ux.i >> 52 & 0x7ff);
	if ((x > 0.0L) ? (y > x) : (y < x)) ai = ux.i + 1;
	else ai = ux.i - 1;
	newexp = (unsigned)(ai >> 52 & 0x7ff);

	if (newexp == 0x7ff) feraiseexcept(FE_OVERFLOW | FE_INEXACT);
	else if (newexp == 0 && oldexp != 0) feraiseexcept(FE_UNDERFLOW | FE_INEXACT);

	ux.i = ai;
	return ux.f;
#endif
}

/* nexttoward: identical semantics to nextafter, except y is always a
 * long double regardless of x's type, so x is compared against y at
 * long double precision rather than after rounding y down to x's
 * type. */
double nexttoward(double x, long double y)
{
	union { double f; uint64_t i; } ux = { x };
	uint64_t ai;
	unsigned oldexp, newexp;
	long double lx = (long double)x;

	if (x != x || y != y) return x + (double)y;
	if (lx == y) return (double)y;

	if (x == 0.0) {
		ux.i = ((y < 0.0L) ? (uint64_t)1 << 63 : (uint64_t)0) | (uint64_t)1;
		feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
		return ux.f;
	}

	oldexp = (unsigned)(ux.i >> 52 & 0x7ff);
	if ((x > 0.0) ? (y > lx) : (y < lx)) ai = ux.i + 1;
	else ai = ux.i - 1;
	newexp = (unsigned)(ai >> 52 & 0x7ff);

	if (newexp == 0x7ff) feraiseexcept(FE_OVERFLOW | FE_INEXACT);
	else if (newexp == 0 && oldexp != 0) feraiseexcept(FE_UNDERFLOW | FE_INEXACT);

	ux.i = ai;
	return ux.f;
}

float nexttowardf(float x, long double y)
{
	union { float f; uint32_t i; } ux = { x };
	uint32_t ai;
	unsigned oldexp, newexp;
	long double lx = (long double)x;

	if (x != x || y != y) return x + (float)y;
	if (lx == y) return (float)y;

	if (x == 0.0f) {
		ux.i = ((y < 0.0L) ? 0x80000000u : 0u) | 1u;
		feraiseexcept(FE_UNDERFLOW | FE_INEXACT);
		return ux.f;
	}

	oldexp = (ux.i >> 23) & 0xff;
	if ((x > 0.0f) ? (y > lx) : (y < lx)) ai = ux.i + 1;
	else ai = ux.i - 1;
	newexp = (ai >> 23) & 0xff;

	if (newexp == 0xff) feraiseexcept(FE_OVERFLOW | FE_INEXACT);
	else if (newexp == 0 && oldexp != 0) feraiseexcept(FE_UNDERFLOW | FE_INEXACT);

	ux.i = ai;
	return ux.f;
}

/* Same type for both arguments -- nexttowardl is nextafterl. */
long double nexttowardl(long double x, long double y) { return nextafterl(x, y); }
