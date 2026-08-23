/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* remainder/remquo: IEEE remainder x - n*y where n is x/y rounded to
 * the nearest integer, ties to even (the FPU's default rounding mode,
 * which nothing in this codebase changes outside of a bracketed
 * fesetround()); remquo additionally reports the low >=3 bits of n,
 * signed to match x/y.  __x87_rndint(t, -1) rounds under the current
 * rounding mode, i.e. exactly this. Computed in long double so the
 * double/float forms round only once, at the final cast. */
#include <math.h>
#include <fenv.h>
#include "x87.h"

static long double remainder_impl(long double x, long double y, int *quop)
{
	long double n, r, ax;
	int qneg;

	if (x != x || y != y) return x + y;
	if (__fpclassifyl(x) == FP_INFINITE || y == 0.0L) {
		/* remainder.html/remquo.html: x==+-Inf, or y==+-0 with x not
		 * NaN -> domain error, NaN. */
		feraiseexcept(FE_INVALID);
		if (quop) *quop = 0;
		return (long double)NAN;
	}
	if (__fpclassifyl(y) == FP_INFINITE) {
		/* "If y is +-Inf and x is finite, then remainder(x,y) returns
		 * x." *quo is unspecified when the reduction is trivial like
		 * this; 0 is as good a choice as any. */
		if (quop) *quop = 0;
		return x;
	}

	qneg = (x < 0.0L) != (y < 0.0L);
	n = __x87_rndint(x / y, -1);
	r = x - n * y;
	/* "If the result equals 0, then it has the same sign as x." */
	if (r == 0.0L) r = copysignl(r, x);

	if (quop) {
		/* Reduce the (possibly huge) quotient's magnitude mod 8 in long
		 * double first -- casting a large-magnitude long double
		 * straight to an integer type would be undefined behaviour. */
		ax = n < 0.0L ? -n : n;
		*quop = qneg ? -(int)__x87_fmod(ax, 8.0L) : (int)__x87_fmod(ax, 8.0L);
	}
	return r;
}

double remainder(double x, double y) { return (double)remainder_impl(x, y, (void *)0); }
float remainderf(float x, float y) { return (float)remainder_impl(x, y, (void *)0); }
long double remainderl(long double x, long double y) { return remainder_impl(x, y, (void *)0); }

double remquo(double x, double y, int *quo) { return (double)remainder_impl(x, y, quo); }
float remquof(float x, float y, int *quo) { return (float)remainder_impl(x, y, quo); }
long double remquol(long double x, long double y, int *quo) { return remainder_impl(x, y, quo); }
