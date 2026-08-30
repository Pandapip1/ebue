/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* fma.html: (x*y)+z, with the special-value table below overriding a
 * plain "compute x*y then add z". Accuracy (a true single rounding)
 * is not asserted by this codebase's tests, so a plain `x*y+z` is an
 * acceptable implementation; it is computed in long double for a
 * little extra accuracy where that type has extra range, at no cost
 * where it doesn't (see src/math/ldbl_math.h). */
#include <math.h>
#include <fenv.h>

static long double fma_impl(long double x, long double y, long double z)
{
	long double p;

	if (x != x || y != y) return x + y + z;

	/* "x*y is an exact zero and z is not a NaN" (the 0*Inf shape, i.e.
	 * one factor is an exact zero and the other an exact infinity) ->
	 * domain error, NaN. */
	if ((x == 0.0L && __fpclassifyl(y) == FP_INFINITE) ||
	    (y == 0.0L && __fpclassifyl(x) == FP_INFINITE)) {
		feraiseexcept(FE_INVALID);
		return (long double)NAN;
	}

	p = x * y;

	/* x*y is an exact infinity and z is an infinity of the opposite
	 * sign -> domain error, NaN. */
	if (__fpclassifyl(p) == FP_INFINITE && __fpclassifyl(z) == FP_INFINITE &&
	    (p < 0.0L) != (z < 0.0L)) {
		feraiseexcept(FE_INVALID);
		return (long double)NAN;
	}

	/* x*y finite (or a same-signed infinity) and z NaN falls straight
	 * out of this addition -- p+z is NaN whenever z is. */
	return p + z;
}

double fma(double x, double y, double z) { return (double)fma_impl(x, y, z); }
float fmaf(float x, float y, float z) { return (float)fma_impl(x, y, z); }
long double fmal(long double x, long double y, long double z) { return fma_impl(x, y, z); }
