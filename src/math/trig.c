/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* sin/cos/tan/atan/atan2 via fsin/fcos/fptan/fpatan.
 *
 * Accuracy: fpatan is within 1 ulp of the 80-bit format everywhere.
 * fsin/fcos/fptan reduce with a 66-bit approximation of pi, so results
 * lose accuracy for arguments extremely close to a nonzero multiple of
 * pi/2, and the instructions only accept |x| < 2^63; larger arguments
 * are first reduced by fmodl(x, 2pi), which costs the usual
 * catastrophic-cancellation accuracy of naive argument reduction --
 * but a double of that size has an ulp of ~2^11 anyway. */
#include <math.h>
#include "x87.h"

static const long double twopi = 6.283185307179586476925286766559L;

static long double reduce(long double x)
{
	if (fabsl(x) < 9.0e18L) return x;
	return __x87_fmod(x, twopi);
}

long double sinl(long double x)
{
	if (__fpclassifyl(x) <= FP_INFINITE) return (x - x) / (x - x);  /* nan for nan/inf */
	return __x87_sin(reduce(x));
}

long double cosl(long double x)
{
	if (__fpclassifyl(x) <= FP_INFINITE) return (x - x) / (x - x);
	return __x87_cos(reduce(x));
}

long double tanl(long double x)
{
	if (__fpclassifyl(x) <= FP_INFINITE) return (x - x) / (x - x);
	return __x87_tan(reduce(x));
}

long double atanl(long double x)
{
	if (x != x) return x;
	return __x87_atan2(x, 1.0L);
}

long double atan2l(long double y, long double x)
{
	if (x != x || y != y) return x + y;
	return __x87_atan2(y, x);
}

double sin(double x) { return (double)sinl(x); }
float sinf(float x) { return (float)sinl(x); }
double cos(double x) { return (double)cosl(x); }
float cosf(float x) { return (float)cosl(x); }
double tan(double x) { return (double)tanl(x); }
float tanf(float x) { return (float)tanl(x); }
double atan(double x) { return (double)atanl(x); }
float atanf(float x) { return (float)atanl(x); }
double atan2(double y, double x) { return (double)atan2l(y, x); }
float atan2f(float y, float x) { return (float)atan2l(y, x); }
