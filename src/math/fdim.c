/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* fdim.html: "the positive difference": x-y if x>y, else +0; NaN if
 * either argument is NaN; range error (overflow) if the positive
 * difference overflows. */
#include <math.h>
#include <fenv.h>

double fdim(double x, double y)
{
	double r;
	if (x != x || y != y) return x + y;
	if (x <= y) return 0.0;
	r = x - y;
	if (isinf(r) && isfinite(x) && isfinite(y)) feraiseexcept(FE_OVERFLOW);
	return r;
}

float fdimf(float x, float y)
{
	float r;
	if (x != x || y != y) return x + y;
	if (x <= y) return 0.0f;
	r = x - y;
	if (isinf(r) && isfinite(x) && isfinite(y)) feraiseexcept(FE_OVERFLOW);
	return r;
}

long double fdiml(long double x, long double y)
{
	long double r;
	if (x != x || y != y) return x + y;
	if (x <= y) return 0.0L;
	r = x - y;
	if (isinf(r) && isfinite(x) && isfinite(y)) feraiseexcept(FE_OVERFLOW);
	return r;
}
