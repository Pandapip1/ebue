/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* round: away-from-zero ties, built on trunc; x - trunc(x) is exact, so
 * this is exact. */
#include <math.h>
#include <fenv.h>
#include <limits.h>
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

/* lround/llround/lrint/llrint: lround.html/lrint.html ERRORS -- "A
 * domain error shall occur if x is NaN, or infinite, or the correct
 * value is not representable as [the return type]", and
 * math_errhandling here is unconditionally MATH_ERREXCEPT
 * (include/math.h), so FE_INVALID is required in every one of those
 * cases, not merely whatever a raw `(long)x`/`(long long)x` cast
 * happens to do. That raw cast is also undefined behaviour in C
 * itself for a NaN or out-of-range operand -- harmless in practice
 * under this project's PE/tcc target (which never traps on it, and
 * happened to raise FE_INVALID incidentally via the hardware
 * fistp/cvttsd2si on x86_64 but silently raised nothing at all on
 * i386), but a real, sanitizer-visible defect under `make asan`'s
 * native clang/UBSan build. Checking the rounded value's range
 * explicitly before the cast fixes both problems at once: it is
 * always well-defined, and it makes FE_INVALID's presence consistent
 * across arches instead of an accident of which cast instruction the
 * compiler happened to emit. LONG_MIN/LLONG_MIN is returned as the
 * "unspecified value" the spec permits -- matching this
 * implementation's own pre-existing x86_64 behaviour, which the
 * regression test in test/posix-math.c pins down (informationally;
 * the value itself is never spec-required). 2^31/2^63 are used
 * instead of LONG_MAX/LLONG_MAX directly because LLONG_MAX (2^63-1)
 * is not exactly representable as a long double on this target and
 * would round up to 2^63 itself, silently admitting one
 * out-of-range value at the boundary. */
long lround(double x)
{
	long double r = roundl(x);
	if (!(r == r) || r >= 2147483648.0L || r < -2147483648.0L) {
		feraiseexcept(FE_INVALID);
		return LONG_MIN;
	}
	return (long)r;
}

long long llround(double x)
{
	long double r = roundl(x);
	if (!(r == r) || r >= 9223372036854775808.0L || r < -9223372036854775808.0L) {
		feraiseexcept(FE_INVALID);
		return LLONG_MIN;
	}
	return (long long)r;
}

long lrint(double x)
{
	long double r = __x87_rndint(x, -1);
	if (!(r == r) || r >= 2147483648.0L || r < -2147483648.0L) {
		feraiseexcept(FE_INVALID);
		return LONG_MIN;
	}
	return (long)r;
}

long long llrint(double x)
{
	long double r = __x87_rndint(x, -1);
	if (!(r == r) || r >= 9223372036854775808.0L || r < -9223372036854775808.0L) {
		feraiseexcept(FE_INVALID);
		return LLONG_MIN;
	}
	return (long long)r;
}

/* lroundf/lroundl/llroundf/llroundl/lrintf/lrintl/llrintf/llrintl: the
 * f/l suffix here changes the ARGUMENT type (float/long double), not
 * the return type -- lround()/llround() above already cover the
 * "l"/"ll" return-type prefix for a `double` argument. Same exact
 * pattern as above in every other respect: an explicit range check
 * before the cast (never a raw `(long)x`/`(long long)x` on a NaN or
 * out-of-range value, which is undefined behaviour and, worse,
 * inconsistent about raising FE_INVALID across arches -- see the big
 * comment above), built on roundl()/__x87_rndint() which already take
 * a long double, so a float or long double argument needs no extra
 * promotion dance. */
long lroundf(float x)
{
	long double r = roundl(x);
	if (!(r == r) || r >= 2147483648.0L || r < -2147483648.0L) {
		feraiseexcept(FE_INVALID);
		return LONG_MIN;
	}
	return (long)r;
}

long lroundl(long double x)
{
	long double r = roundl(x);
	if (!(r == r) || r >= 2147483648.0L || r < -2147483648.0L) {
		feraiseexcept(FE_INVALID);
		return LONG_MIN;
	}
	return (long)r;
}

long long llroundf(float x)
{
	long double r = roundl(x);
	if (!(r == r) || r >= 9223372036854775808.0L || r < -9223372036854775808.0L) {
		feraiseexcept(FE_INVALID);
		return LLONG_MIN;
	}
	return (long long)r;
}

long long llroundl(long double x)
{
	long double r = roundl(x);
	if (!(r == r) || r >= 9223372036854775808.0L || r < -9223372036854775808.0L) {
		feraiseexcept(FE_INVALID);
		return LLONG_MIN;
	}
	return (long long)r;
}

long lrintf(float x)
{
	long double r = __x87_rndint(x, -1);
	if (!(r == r) || r >= 2147483648.0L || r < -2147483648.0L) {
		feraiseexcept(FE_INVALID);
		return LONG_MIN;
	}
	return (long)r;
}

long lrintl(long double x)
{
	long double r = __x87_rndint(x, -1);
	if (!(r == r) || r >= 2147483648.0L || r < -2147483648.0L) {
		feraiseexcept(FE_INVALID);
		return LONG_MIN;
	}
	return (long)r;
}

long long llrintf(float x)
{
	long double r = __x87_rndint(x, -1);
	if (!(r == r) || r >= 9223372036854775808.0L || r < -9223372036854775808.0L) {
		feraiseexcept(FE_INVALID);
		return LLONG_MIN;
	}
	return (long long)r;
}

long long llrintl(long double x)
{
	long double r = __x87_rndint(x, -1);
	if (!(r == r) || r >= 9223372036854775808.0L || r < -9223372036854775808.0L) {
		feraiseexcept(FE_INVALID);
		return LLONG_MIN;
	}
	return (long long)r;
}
