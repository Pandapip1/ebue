/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* fmax.html / fmin.html: "shall determine the maximum [minimum] numeric
 * value of their arguments"; RETURN VALUE -- "If just one argument is a
 * NaN, the other argument shall be returned.  If x and y are NaN, a NaN
 * shall be returned."  POSIX adds no ERRORS for either.
 *
 * The standard does not say which of +0 and -0 wins (they compare
 * equal, so either is a permitted "maximum"); this implementation makes
 * fmax prefer +0 and fmin prefer -0, which is what IEC 60559's
 * maxNum/minNum recommend and what the f/l variants below match.
 *
 * The float and long double variants are written out rather than
 * forwarded through the double one: on this project's mingw-w64 builds
 * `long double` is genuine x87 80-bit extended precision (see
 * src/math/ldbl_math.h), so fmaxl(a, b) must not round its operands through
 * double, and fmaxf must not widen a float NaN payload.  Under this
 * tcc's -win32 targets long double *is* double and the l forms are then
 * numerically identical to the double ones -- but that is an ABI
 * property of one compiler, not something to hard-code here. */
#include <math.h>

double fmax(double x, double y)
{
	if (x != x) return y;
	if (y != y) return x;
	/* +0 beats -0 */
	if (x == 0 && y == 0) return __signbit(x) ? y : x;
	return x < y ? y : x;
}

double fmin(double x, double y)
{
	if (x != x) return y;
	if (y != y) return x;
	if (x == 0 && y == 0) return __signbit(x) ? x : y;
	return x < y ? x : y;
}

float fmaxf(float x, float y)
{
	if (x != x) return y;
	if (y != y) return x;
	if (x == 0 && y == 0) return __signbitf(x) ? y : x;
	return x < y ? y : x;
}

float fminf(float x, float y)
{
	if (x != x) return y;
	if (y != y) return x;
	if (x == 0 && y == 0) return __signbitf(x) ? x : y;
	return x < y ? x : y;
}

long double fmaxl(long double x, long double y)
{
	if (x != x) return y;
	if (y != y) return x;
	if (x == 0 && y == 0) return __signbitl(x) ? y : x;
	return x < y ? y : x;
}

long double fminl(long double x, long double y)
{
	if (x != x) return y;
	if (y != y) return x;
	if (x == 0 && y == 0) return __signbitl(x) ? x : y;
	return x < y ? x : y;
}
