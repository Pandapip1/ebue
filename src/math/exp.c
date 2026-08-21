/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* exp/exp-family via the classic f2xm1/fscale sequence: 2^t where
 * t = x*log2(e).  The multiplication by log2(e) rounds once in the
 * 80-bit format, and f2xm1 is within 1 ulp of 80-bit, so the double
 * results are accurate to well within 1 ulp. */
#include <math.h>
#include "x87.h"

static const long double log2e = 1.4426950408889634073599246810019L;

long double expl(long double x)
{
	if (x != x) return x;
	if (x > 11400.0L) return HUGE_VALL;    /* overflows even 80-bit */
	if (x < -11400.0L) return 0.0L;        /* underflows even 80-bit */
	return __x87_exp2(x * log2e);
}

double exp(double x) { return (double)expl(x); }
float expf(float x) { return (float)expl(x); }
