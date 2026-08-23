/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* asinh/acosh/atanh, composed from log1pl/logl/sqrtl rather than any
 * new x87 asm.
 *
 * asinh(x) = ln(x + sqrt(x^2+1)). For x >= 0 (we work in ax = |x| and
 * restore the sign with copysignl at the end, since asinh is odd),
 * x + sqrt(x^2+1) is always >= 1, so subtracting 1 from it before
 * taking the log cannot cancel catastrophically -- but it is still
 * worth doing through log1p, using the standard rationalised form
 *   sqrt(1+ax^2) - 1 == ax^2 / (1 + sqrt(1+ax^2))
 * (multiply by the conjugate) so that the "-1" never has to subtract
 * two nearby floating point values -- it is computed directly as a
 * small, accurate quantity to begin with. For very large ax, ax^2
 * would overflow long before asinh(x) ~ ln(2*ax) does, so that case
 * is handled separately with the asymptotic form.
 *
 * acosh(x) = ln(x + sqrt(x^2-1)), x >= 1. x^2-1 is rewritten as the
 * product (x-1)*(x+1) (avoids cancellation between x^2 and 1 for x
 * near 1), and the "-1" again goes through log1p on x-1 plus that
 * sqrt, so acosh(1.0) reduces to log1p(0.0) == +0.0 exactly. Large x
 * again gets the ln(2x) asymptotic form to sidestep overflow in
 * (x-1)*(x+1).
 *
 * atanh(x) = 0.5*ln((1+x)/(1-x)) = 0.5*log1p(2x/(1-x)) (since
 * (1+x)/(1-x) == 1 + 2x/(1-x)); log1p keeps this accurate for x near
 * 0, where 2x/(1-x) is itself small. */
#include <math.h>
#include <fenv.h>

static const long double ln2 = 0.69314718055994530941723212145818L;

long double asinhl(long double x)
{
	long double ax, r;
	if (x != x) return x;
	if (x == 0.0L) return x;
	if (__fpclassifyl(x) == FP_INFINITE) return x;
	ax = fabsl(x);
	if (ax > 1e150L) r = logl(ax) + ln2;
	else r = log1pl(ax + ax * ax / (1.0L + sqrtl(1.0L + ax * ax)));
	return copysignl(r, x);
}

long double acoshl(long double x)
{
	long double r;
	if (x != x) return x;
	if (x < 1.0L) {
		feraiseexcept(FE_INVALID);
		return (long double)NAN;
	}
	if (x > 1e150L) return logl(x) + ln2;
	r = x - 1.0L;
	return log1pl(r + sqrtl(r * (x + 1.0L)));
}

long double atanhl(long double x)
{
	long double ax;
	if (x != x) return x;
	if (x == 0.0L) return x;
	ax = fabsl(x);
	if (ax > 1.0L) {
		feraiseexcept(FE_INVALID);
		return (long double)NAN;
	}
	if (ax == 1.0L) {
		feraiseexcept(FE_DIVBYZERO);
		return copysignl(HUGE_VALL, x);
	}
	return 0.5L * log1pl(2.0L * x / (1.0L - x));
}

double asinh(double x) { return (double)asinhl(x); }
float asinhf(float x) { return (float)asinhl(x); }
double acosh(double x) { return (double)acoshl(x); }
float acoshf(float x) { return (float)acoshl(x); }
double atanh(double x) { return (double)atanhl(x); }
float atanhf(float x) { return (float)atanhl(x); }
