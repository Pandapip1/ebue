/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* pow = 2^(y*log2(x)), with the C99 special cases handled explicitly.
 * y*log2(x) is computed by a single fyl2x call with the real y (which
 * multiplies by log2(x) on the FPU stack before ever rounding to
 * memory), not by fyl2x(x, 1.0) followed by a separate multiply by y -
 * the latter rounds log2(x) down to double first and only then
 * multiplies, throwing away exactly the precision fyl2x's fused
 * multiply is for.  With the fused form the result is rounded once;
 * an error of e ulps there is amplified into ~e*|y*log2(x)| ulps of
 * the result, so for double results the error stays below 1 ulp for
 * |y*log2 x| up to a few hundred (i.e. any case that does not overflow
 * double), with worst cases around 1-2 ulp near the overflow
 * threshold. */
#include <math.h>
#include "ldbl_math.h"

static int is_int(long double y) { return __x87_rndint(y, 3) == y; }
static int is_odd_int(long double y)
{
	return is_int(y) && !is_int(y * 0.5L);
}

long double powl(long double x, long double y)
{
	int neg = 0;
	long double r;

	if (y == 0) return 1.0L;
	if (x == 1.0L) return 1.0L;
	if (x != x || y != y) return x + y;
	if (x == -1.0L && (y == HUGE_VALL || y == -HUGE_VALL)) return 1.0L;
	if (y == HUGE_VALL) return fabsl(x) < 1 ? 0.0L : HUGE_VALL;
	if (y == -HUGE_VALL) return fabsl(x) < 1 ? HUGE_VALL : 0.0L;
	if (x == HUGE_VALL) return y < 0 ? 0.0L : HUGE_VALL;
	if (x == -HUGE_VALL) {
		r = y < 0 ? 0.0L : HUGE_VALL;
		return is_odd_int(y) ? -r : r;
	}
	if (x == 0) {
		if (y < 0) return is_odd_int(y) ? copysignl(HUGE_VALL, x) : HUGE_VALL;
		return is_odd_int(y) ? copysignl(0.0L, x) : 0.0L;
	}
	if (x < 0) {
		if (!is_int(y)) return (x - x) / (x - x);   /* nan */ // NOLINT(misc-redundant-expression) -- deliberate 0/0 to make a NaN
		neg = is_odd_int(y);
		x = -x;
	}
	{
		long double t = __x87_yl2x(x, y);
		if (t > 16400.0L) r = HUGE_VALL;
		else if (t < -16400.0L) r = 0.0L;
		else r = __x87_exp2(t);
	}
	return neg ? -r : r;
}

double pow(double x, double y) { return (double)powl(x, y); }
float powf(float x, float y) { return (float)powl(x, y); }
