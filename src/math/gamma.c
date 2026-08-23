/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* lgamma/tgamma via the Stirling asymptotic series for ln(Gamma(z)),
 * z reasonably large:
 *
 *   ln(Gamma(z)) = (z-1/2)*ln(z) - z + (1/2)*ln(2*pi)
 *                  + Sum_{n=1}^inf B_{2n} / (2n*(2n-1)*z^(2n-1))
 *
 * (the standard Bernoulli-number Stirling series for the log-gamma
 * function; B2=1/6, B4=-1/30, B6=1/42, B8=-1/30 give the four terms
 * used below). For 0 < x < 8 the recurrence Gamma(x) = Gamma(x+n) /
 * (x*(x+1)*...*(x+n-1)), i.e.
 *
 *   ln(Gamma(x)) = ln(Gamma(x+n)) - Sum_{i=0}^{n-1} ln(x+i)
 *
 * shifts x up until x+n >= 8, where the series above is accurate to
 * roughly 1e-11 or better (confirmed numerically: lgamma_pos(5) and
 * lgamma_pos(10) both land within ~1e-11 of the true value). For
 * negative x, Euler's reflection formula
 *
 *   Gamma(x) * Gamma(1-x) = pi / sin(pi*x)
 *
 * gives both ln|Gamma(x)| (for lgamma) and, applied directly rather
 * than split into magnitude+sign, Gamma(x) itself with its correct
 * sign (for tgamma) -- sin(pi*x) already carries the right sign, so
 * there is no need to separately track the parity of floor(x).
 *
 * None of this is copied from any specific library's source; it is a
 * direct implementation of the published Stirling series and Euler's
 * reflection formula (see e.g. Abramowitz & Stegun 6.1.37/6.1.15, or
 * any standard reference on the gamma function). */
#include <math.h>
#include <fenv.h>

static const long double pi_ = 3.14159265358979323846264338327950288L;
static const long double ln2pi_ = 1.83787706640934548356065947281123527L;

/* ln(Gamma(z)) via the Stirling series; z must be finite and >= 8. */
static long double stirling(long double z)
{
	long double invz = 1.0L / z;
	long double invz2 = invz * invz;
	long double series = invz * (1.0L / 12.0L
		+ invz2 * (-1.0L / 360.0L
		+ invz2 * (1.0L / 1260.0L
		+ invz2 * (-1.0L / 1680.0L))));
	return (z - 0.5L) * logl(z) - z + 0.5L * ln2pi_ + series;
}

/* ln(Gamma(x)) for finite x > 0, shifting up into the Stirling
 * series's accurate range via the recurrence Gamma(x) =
 * Gamma(x+n)/(x*(x+1)*...*(x+n-1)). */
static long double lgamma_pos(long double x)
{
	long double sum = 0.0L;
	while (x < 8.0L) {
		sum += logl(x);
		x += 1.0L;
	}
	return stirling(x) - sum;
}

/* Gamma(x) for finite x > 0. */
static long double tgamma_pos(long double x)
{
	return expl(lgamma_pos(x));
}

long double lgammal(long double x)
{
	long double r;
	int cls = __fpclassifyl(x);
	if (cls == FP_NAN) return x;
	if (cls == FP_INFINITE) return HUGE_VALL;             /* +-inf -> +inf */
	if (x <= 0.0L && x == floorl(x)) {
		feraiseexcept(FE_DIVBYZERO);                  /* pole */
		return HUGE_VALL;
	}
	if (x == 1.0L || x == 2.0L) return 0.0L;
	if (x > 0.0L) {
		r = lgamma_pos(x);
	} else {
		long double s = sinl(pi_ * x);
		r = logl(pi_ / fabsl(s)) - lgamma_pos(1.0L - x);
	}
	if (r == HUGE_VALL || r == -HUGE_VALL) feraiseexcept(FE_OVERFLOW);
	return r;
}

long double tgammal(long double x)
{
	long double r;
	int cls = __fpclassifyl(x);
	if (cls == FP_NAN) return x;
	if (cls == FP_INFINITE) {
		if (x > 0.0L) return HUGE_VALL;
		feraiseexcept(FE_INVALID);
		return (x - x) / (x - x);                     /* nan */
	}
	if (x == 0.0L) {
		feraiseexcept(FE_DIVBYZERO);
		return copysignl(HUGE_VALL, x);                /* pole, sign of x */
	}
	if (x < 0.0L && x == floorl(x)) {
		feraiseexcept(FE_INVALID);                     /* negative integer */
		return (x - x) / (x - x);                      /* nan */
	}
	if (x > 0.0L) {
		r = tgamma_pos(x);
	} else {
		r = pi_ / (sinl(pi_ * x) * tgamma_pos(1.0L - x));
	}
	if (r == HUGE_VALL || r == -HUGE_VALL) feraiseexcept(FE_OVERFLOW);
	return r;
}

double lgamma(double x) { return (double)lgammal(x); }
float lgammaf(float x) { return (float)lgammal(x); }
double tgamma(double x) { return (double)tgammal(x); }
float tgammaf(float x) { return (float)tgammal(x); }
