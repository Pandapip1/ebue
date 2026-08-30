/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* scalbln.html: x * FLT_RADIX^n, n a long. Identical to scalbn()
 * (src/math/scalbn.c) except for the argument's width; a long outside
 * int's range is clamped to INT_MIN/INT_MAX first (rather than cast
 * directly, which would be undefined for an out-of-range long) -- any
 * such n already forces overflow or underflow for every finite,
 * nonzero x, so INT_MIN/INT_MAX (themselves already enormous exponents
 * for a double or even an 80-bit long double) preserve that outcome
 * exactly. scalbn()/__x87_scalbn() already produce the correctly
 * signed +-HUGE_VAL or 0.0/subnormal via the real x87 fscale+store
 * hardware overflow/underflow behaviour (see src/math/ldbl_math.h and
 * src/math/scalbn.c, which raise nothing explicitly either), so this
 * follows that same, already-established convention rather than
 * duplicating it with explicit feraiseexcept() calls. */
#include <math.h>
#include <limits.h>

double scalbln(double x, long n)
{
	if (n > INT_MAX) n = INT_MAX;
	else if (n < INT_MIN) n = INT_MIN;
	return scalbn(x, (int)n);
}

float scalblnf(float x, long n)
{
	if (n > INT_MAX) n = INT_MAX;
	else if (n < INT_MIN) n = INT_MIN;
	return scalbnf(x, (int)n);
}

long double scalblnl(long double x, long n)
{
	if (n > INT_MAX) n = INT_MAX;
	else if (n < INT_MIN) n = INT_MIN;
	return scalbnl(x, (int)n);
}
