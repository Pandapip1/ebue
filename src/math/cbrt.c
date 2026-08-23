/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* cbrt via exp(log(x)/3) for an initial estimate, then one Newton
 * step on f(y) = y^3 - x:
 *   y' = y - (y^3-x)/(3y^2) = (2y + x/y^2)/3
 * exp/log are each within about 1ulp of the 80-bit format (see
 * exp.c/log.c), so the seed is accurate to a relative error on the
 * order of 2^-52; Newton's method for a simple root roughly squares
 * the relative error per step, so one step brings that down to
 * around 2^-104, i.e. the result should be correctly rounded to
 * double for ordinary arguments -- in particular cbrt(27.0) is
 * expected to land on exactly 3.0, not merely "close to" it.
 *
 * cbrt.html: "No errors are defined" -- cbrt is defined for every
 * finite x (including negative x, unlike sqrt/log), so there is no
 * domain/pole/range case here that needs feraiseexcept at all. */
#include <math.h>

long double cbrtl(long double x)
{
	long double ax, y;
	if (x != x) return x;
	if (x == 0.0L) return x;                        /* preserves the sign of zero */
	if (__fpclassifyl(x) == FP_INFINITE) return x;   /* preserves the sign of infinity */
	ax = fabsl(x);
	y = expl(logl(ax) / 3.0L);
	y = (2.0L * y + ax / (y * y)) / 3.0L;            /* one Newton refinement */
	return copysignl(y, x);
}

double cbrt(double x) { return (double)cbrtl(x); }
float cbrtf(float x) { return (float)cbrtl(x); }
