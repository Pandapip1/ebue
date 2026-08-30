/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* exp/exp-family via the classic f2xm1/fscale sequence: 2^t where
 * t = x*log2(e).  The multiplication by log2(e) rounds once in the
 * 80-bit format, and f2xm1 is within 1 ulp of 80-bit, so the double
 * results are accurate to well within 1 ulp. */
#include <math.h>
#include "ldbl_math.h"

static const long double log2e = 1.4426950408889634073599246810019L;

long double expl(long double x)
{
	if (x != x) return x;
	if (x > 11400.0L) return HUGE_VALL;    /* overflows even 80-bit */
	if (x < -11400.0L) return 0.0L;        /* underflows even 80-bit */
	return __x87_exp2(x * log2e);
}

/* exp2.html: "compute the base-2 exponential of x", i.e. 2^x.
 * RETURN VALUE -- NaN -> NaN; +Inf -> +Inf; -Inf -> +0; +-0 -> 1;
 * overflow -> range error and +-HUGE_VAL (here +HUGE_VAL, 2^x being
 * positive throughout); underflow -> range error and a value no
 * greater than DBL_MIN (0.0 is the permitted IEC 60559 answer, and
 * what f2xm1/fscale produce).
 *
 * This is what __x87_exp2 already computes for exp()/pow(), with no
 * log2(e) multiplication in front of it -- so exp2 is the *more*
 * accurate of the two here, not a wrapper around a less accurate one:
 * it has no argument-reduction rounding at all, and f2xm1 is within
 * 1 ulp of the 80-bit format.
 *
 * The two range guards are what keep +-Inf out of __x87_exp2: frndint
 * of an infinity is that infinity, and the fsub that follows would
 * then evaluate Inf-Inf and yield a NaN.  The thresholds are outside
 * the 80-bit exponent range (2^16384 overflows, 2^-16445 is below the
 * smallest 80-bit subnormal), so no finite argument that could produce
 * a representable result is caught by them; a finite argument that
 * overflows *double* -- 2^1024 and up -- is computed exactly in the
 * 80-bit registers and rounds to +Inf at the store, which is the
 * range-error result the spec asks for.  Subnormal results likewise
 * arise from the store rounding, not from a special case here. */
long double exp2l(long double x)
{
	if (x != x) return x;
	if (x > 16384.0L) return HUGE_VALL;
	if (x < -16446.0L) return 0.0L;
	return __x87_exp2(x);
}

double exp(double x) { return (double)expl(x); }
float expf(float x) { return (float)expl(x); }
double exp2(double x) { return (double)exp2l(x); }
float exp2f(float x) { return (float)exp2l(x); }
