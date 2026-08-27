/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* sinh/cosh/tanh, composed from expm1l/expl (and, for tanh, expm1l
 * again on 2x) rather than duplicating any exp-family x87 asm here.
 *
 * sinh(x) = 0.5*(e^x - e^-x). Written straight that way it cancels
 * badly for small x (e^x and e^-x are both close to 1). Using
 * t = expm1(x) = e^x-1: e^-x = 1/e^x = 1/(t+1), so
 *   0.5*(e^x - e^-x) = 0.5*((t+1) - 1/(t+1)) = 0.5*(t + t/(t+1))
 * (since (t+1) - 1/(t+1) = ((t+1)^2-1)/(t+1) = (t^2+2t)/(t+1) =
 * t*(t+2)/(t+1) = t*(1 + 1/(t+1)) = t + t/(t+1)), and both t and
 * t/(t+1) are already accurate near 0 because expm1 is.
 *
 * cosh(x) = 0.5*(e^x + e^-x) has no such cancellation (both terms are
 * positive and near 1 for small x, and their sum is exactly what is
 * wanted), so it is built directly from expl.
 *
 * tanh(x) = (e^2x-1)/(e^2x+1) = expm1(2x)/(expm1(2x)+2), which is
 * exact at x==0 (expm1(0)==0) and accurate near it for the same
 * reason as sinh above. */
#include <math.h>
#include <fenv.h>

long double sinhl(long double x)
{
	long double t;
	if (x != x) return x;
	if (x == 0.0L) return x;               /* preserves the sign of zero */
	if (x < 0.0L) return -sinhl(-x);       /* avoid expm1(x) rounding to -1 */
	t = expm1l(x);
	if (t == HUGE_VALL) {                  /* x large enough that e^x overflowed */
		feraiseexcept(FE_OVERFLOW);
		return HUGE_VALL;
	}
	return 0.5L * (t + t / (t + 1.0L));
}

long double coshl(long double x)
{
	long double e;
	if (x != x) return x;
	e = expl(fabsl(x));
	if (e == HUGE_VALL) {
		feraiseexcept(FE_OVERFLOW);
		return HUGE_VALL;
	}
	return 0.5L * (e + 1.0L / e);
}

long double tanhl(long double x)
{
	long double ax, t, r;
	if (x != x) return x;
	if (x == 0.0L) return x;               /* preserves the sign of zero */
	ax = fabsl(x);
	t = expm1l(2.0L * ax);
	r = (t == HUGE_VALL) ? 1.0L : t / (t + 2.0L);
	return copysignl(r, x);
}

double sinh(double x) { return (double)sinhl(x); }
float sinhf(float x) { return (float)sinhl(x); }
double cosh(double x) { return (double)coshl(x); }
float coshf(float x) { return (float)coshl(x); }
double tanh(double x) { return (double)tanhl(x); }
float tanhf(float x) { return (float)tanhl(x); }
