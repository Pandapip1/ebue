/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* round: away-from-zero ties, built on trunc; x - trunc(x) is exact, so
 * this is exact. */
#include <math.h>
#include "x87.h"

long double roundl(long double x)
{
	long double t;
	if (!(__fpclassifyl(x) > FP_INFINITE)) return x;   /* nan, inf */
	t = __x87_rndint(x, 3);
	if (x - t >= 0.5L) t += 1.0L;
	else if (x - t <= -0.5L) t -= 1.0L;
	if (t == 0) return copysignl(t, x);
	return t;
}

double round(double x) { return (double)roundl(x); }
float roundf(float x) { return (float)roundl(x); }

long lround(double x) { return (long)roundl(x); }
long long llround(double x) { return (long long)roundl(x); }
long lrint(double x) { return (long)__x87_rndint(x, -1); }
long long llrint(double x) { return (long long)__x87_rndint(x, -1); }
