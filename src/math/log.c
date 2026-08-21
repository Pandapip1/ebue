/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* log/log2/log10 via fyl2x (y*log2(x)): within 1 ulp of the 80-bit
 * format, so well within 1 ulp of double after the final rounding.
 * fyl2x itself produces -inf for +-0 (with divide-by-zero), nan for
 * negative input (invalid), +inf for +inf. */
#include <math.h>
#include "x87.h"

static const long double ln2 = 0.69314718055994530941723212145818L;
static const long double lg2 = 0.30102999566398119521373889472449L;

long double logl(long double x)
{
	if (x != x) return x;
	return __x87_yl2x(x, ln2);
}

long double log2l(long double x)
{
	if (x != x) return x;
	return __x87_yl2x(x, 1.0L);
}

long double log10l(long double x)
{
	if (x != x) return x;
	return __x87_yl2x(x, lg2);
}

double log(double x) { return (double)logl(x); }
float logf(float x) { return (float)logl(x); }
double log2(double x) { return (double)log2l(x); }
float log2f(float x) { return (float)log2l(x); }
double log10(double x) { return (double)log10l(x); }
float log10f(float x) { return (float)log10l(x); }
