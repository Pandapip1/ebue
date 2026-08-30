/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <math.h>
#include "ldbl_math.h"

long double modfl(long double x, long double *ip)
{
	long double t;
	int cls = __fpclassifyl(x);
	if (cls == FP_NAN) { *ip = x; return x; }
	if (cls == FP_INFINITE) { *ip = x; return copysignl(0.0L, x); }
	t = __x87_rndint(x, 3);
	*ip = t;
	return copysignl(x - t, x);
}

double modf(double x, double *ip)
{
	long double i;
	double r = (double)modfl(x, &i);
	*ip = (double)i;
	return r;
}

float modff(float x, float *ip)
{
	long double i;
	float r = (float)modfl(x, &i);
	*ip = (float)i;
	return r;
}
