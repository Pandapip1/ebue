/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* nearbyint.html: rounds under the current rounding direction, exactly
 * like rint() (src/math/floor.c), except "No errors are defined" and,
 * critically, it must NOT raise the inexact exception -- the one
 * clause that distinguishes it from rint(). __x87_rndint's underlying
 * frndint DOES raise FE_INEXACT in hardware when the result differs
 * from the input, so that flag is snapshotted before rounding and, if
 * frndint is the one that set it, cleared again afterward -- leaving
 * every other flag (and a pre-existing FE_INEXACT) untouched. */
#include <math.h>
#include <fenv.h>
#include "ldbl_math.h"

double nearbyint(double x)
{
	int had = fetestexcept(FE_INEXACT);
	double r = (double)__x87_rndint(x, -1);
	if (!had && fetestexcept(FE_INEXACT)) (void)feclearexcept(FE_INEXACT);
	return r;
}

float nearbyintf(float x)
{
	int had = fetestexcept(FE_INEXACT);
	float r = (float)__x87_rndint(x, -1);
	if (!had && fetestexcept(FE_INEXACT)) (void)feclearexcept(FE_INEXACT);
	return r;
}

long double nearbyintl(long double x)
{
	int had = fetestexcept(FE_INEXACT);
	long double r = __x87_rndint(x, -1);
	if (!had && fetestexcept(FE_INEXACT)) (void)feclearexcept(FE_INEXACT);
	return r;
}
