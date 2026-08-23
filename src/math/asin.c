/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* asin/acos via atan2l: fpatan (behind atan2l, see trig.c) already
 * handles quadrant selection and signed-zero results in hardware, so
 * composing on top of it keeps asin/acos within about 1ulp of double
 * without any extra argument-reduction work of our own.
 *
 * asin(x) = atan2(x, sqrt(1-x^2)): the sqrt is always >= 0, so the
 * quadrant of the atan2 result tracks the sign of x, exactly like
 * asin. acos(x) = atan2(sqrt(1-x^2), x) is the same construction with
 * the arguments swapped, which is what gives it a nonnegative range
 * (and, at x == 1.0, the required +0.0 result: atan2(+0, +1) == +0). */
#include <math.h>
#include <fenv.h>

long double asinl(long double x)
{
	if (x != x) return x;
	if (fabsl(x) > 1.0L) {
		feraiseexcept(FE_INVALID);
		return (long double)NAN;
	}
	return atan2l(x, sqrtl(1.0L - x * x));
}

long double acosl(long double x)
{
	if (x != x) return x;
	if (fabsl(x) > 1.0L) {
		feraiseexcept(FE_INVALID);
		return (long double)NAN;
	}
	return atan2l(sqrtl(1.0L - x * x), x);
}

double asin(double x) { return (double)asinl(x); }
float asinf(float x) { return (float)asinl(x); }
double acos(double x) { return (double)acosl(x); }
float acosf(float x) { return (float)acosl(x); }
