/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* ilogb.html/logb.html: logb(x) is frexp(x)'s exponent minus one (frexp
 * returns a mantissa in [0.5,1) with x == mantissa * 2^e, so x ==
 * (2*mantissa) * 2^(e-1) with 2*mantissa in [1,2), the usual
 * "unbiased exponent" convention); frexp already normalises subnormal
 * x correctly (see src/math/frexp.c), so this is exact for every
 * finite nonzero x. ilogb is the same, returned as int, with three
 * reserved out-of-band results for 0/NaN/Inf (FP_ILOGB0 and
 * FP_ILOGBNAN are already defined in include/math.h). */
#include <math.h>
#include <fenv.h>
#include <limits.h>

double logb(double x)
{
	int e;
	if (x != x) return x;
	if (__fpclassify(x) == FP_INFINITE) return HUGE_VAL;
	if (x == 0.0) {
		feraiseexcept(FE_DIVBYZERO);
		return -HUGE_VAL;
	}
	frexp(x, &e);
	return (double)(e - 1);
}

float logbf(float x)
{
	int e;
	if (x != x) return x;
	if (__fpclassifyf(x) == FP_INFINITE) return HUGE_VALF;
	if (x == 0.0f) {
		feraiseexcept(FE_DIVBYZERO);
		return -HUGE_VALF;
	}
	frexpf(x, &e);
	return (float)(e - 1);
}

long double logbl(long double x)
{
	int e;
	if (x != x) return x;
	if (__fpclassifyl(x) == FP_INFINITE) return HUGE_VALL;
	if (x == 0.0L) {
		feraiseexcept(FE_DIVBYZERO);
		return -HUGE_VALL;
	}
	frexpl(x, &e);
	return (long double)(e - 1);
}

int ilogb(double x)
{
	int e;
	if (x != x) { feraiseexcept(FE_INVALID); return FP_ILOGBNAN; }
	if (__fpclassify(x) == FP_INFINITE) { feraiseexcept(FE_INVALID); return INT_MAX; }
	if (x == 0.0) { feraiseexcept(FE_INVALID); return FP_ILOGB0; }
	frexp(x, &e);
	return e - 1;
}

int ilogbf(float x)
{
	int e;
	if (x != x) { feraiseexcept(FE_INVALID); return FP_ILOGBNAN; }
	if (__fpclassifyf(x) == FP_INFINITE) { feraiseexcept(FE_INVALID); return INT_MAX; }
	if (x == 0.0f) { feraiseexcept(FE_INVALID); return FP_ILOGB0; }
	frexpf(x, &e);
	return e - 1;
}

int ilogbl(long double x)
{
	int e;
	if (x != x) { feraiseexcept(FE_INVALID); return FP_ILOGBNAN; }
	if (__fpclassifyl(x) == FP_INFINITE) { feraiseexcept(FE_INVALID); return INT_MAX; }
	if (x == 0.0L) { feraiseexcept(FE_INVALID); return FP_ILOGB0; }
	frexpl(x, &e);
	return e - 1;
}
