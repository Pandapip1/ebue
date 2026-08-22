/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of math.h, concentrating on what
 * test/math.c (a broad sanity pass, not clause-cited) does not already
 * pin down: special-value tables (±0, ±Inf, NaN), sign of zero (which
 * == cannot distinguish -- use signbit()), and the math_errhandling /
 * errno contract.  Each assertion cites the clause of
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html
 * it checks.  See test/posix-coverage/math.md for the full ledger.
 *
 * POSIX does not mandate a specific accuracy for the transcendental
 * functions (only math_errhandling's special-value table is a "shall"),
 * so this file does not assert ulp figures; where accuracy is worth
 * recording it is printed as information, never as a failing CHECK.
 * Every assertion below is either cited to a "shall" clause or
 * explicitly marked "informational" in a comment.
 *
 * arch/i386 runs the x87 unit at 80-bit internal precision even though
 * ntlibc's own `long double` is 64-bit on the NT target (see
 * src/math/x87.h); results can legitimately differ in the last bit(s)
 * between arches for the same reason test/math.c is skipped on host
 * ASan builds (tools/asan-build.sh, not_native()).  Run `make check`
 * on both arches and compare rather than assuming one is correct.
 */
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <errno.h>
#include <fenv.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int negzero(double x) { return x == 0.0 && signbit(x); }
static int poszero(double x) { return x == 0.0 && !signbit(x); }

/* ---- fabs.html: RETURN VALUE -- NaN->NaN, ±0->+0, ±Inf->+Inf ---- */
static void test_fabs(void)
{
	CHECK(isnan(fabs(NAN)));
	CHECK(poszero(fabs(0.0)) && poszero(fabs(-0.0)));
	CHECK(fabs(HUGE_VAL) == HUGE_VAL && fabs(-HUGE_VAL) == HUGE_VAL);
	CHECK(isnan(fabsf(NAN)) && isnan(fabsl(NAN)));
	CHECK(poszero(fabsl(-0.0L)));
}

/* ---- copysign.html: RETURN VALUE -- "a value with the magnitude of x
 * and the sign of y" ---- */
static void test_copysign(void)
{
	CHECK(copysign(3.0, -1.0) == -3.0 && copysign(-3.0, 1.0) == 3.0);
	/* magnitude of x is taken even when x is negative/NaN-tagged */
	CHECK(copysign(-5.0, -1.0) == -5.0);
	/* sign of a zero y controls the result's sign */
	CHECK(copysign(4.0, -0.0) == -4.0 && copysign(-4.0, 0.0) == 4.0);
	/* and a zero magnitude x still takes that sign, distinguishable
	 * only via signbit() */
	CHECK(negzero(copysign(0.0, -0.0)));
	CHECK(poszero(copysign(-0.0, 0.0)));
	/* magnitude of a NaN x: still "the magnitude of x" -- result is a
	 * NaN either way, but must not become non-NaN */
	CHECK(isnan(copysign(NAN, -1.0)) && isnan(copysign(NAN, 1.0)));
	/* sign of an Inf y */
	CHECK(copysign(1.0, -HUGE_VAL) == -1.0);
	CHECK(copysignl(3.0L, -1.0L) == -3.0L);
}

/* ---- fpclassify.html / isnan.html / isinf.html / isfinite.html /
 * isnormal.html / signbit.html: classification of each category ---- */
static void test_classify(void)
{
	CHECK(fpclassify(0.0) == FP_ZERO && fpclassify(-0.0) == FP_ZERO);
	CHECK(fpclassify(1.0) == FP_NORMAL);
	CHECK(fpclassify(HUGE_VAL) == FP_INFINITE && fpclassify(-HUGE_VAL) == FP_INFINITE);
	CHECK(fpclassify(NAN) == FP_NAN);
	CHECK(fpclassify(5e-324) == FP_SUBNORMAL && fpclassify(-5e-324) == FP_SUBNORMAL);

	CHECK(isnan(NAN) && !isnan(1.0) && !isnan(HUGE_VAL));
	CHECK(isinf(HUGE_VAL) && isinf(-HUGE_VAL) && !isinf(NAN) && !isinf(1.0));
	CHECK(isfinite(0.0) && isfinite(5e-324) && isfinite(DBL_MAX));
	CHECK(!isfinite(HUGE_VAL) && !isfinite(-HUGE_VAL) && !isfinite(NAN));
	CHECK(isnormal(1.0) && isnormal(DBL_MIN));
	CHECK(!isnormal(5e-324) && !isnormal(0.0) && !isnormal(HUGE_VAL) && !isnormal(NAN));

	/* signbit.html: "NaNs, zeros, and infinities have a sign bit" --
	 * non-zero iff negative, independent of == comparability */
	CHECK(signbit(-0.0) && !signbit(0.0));
	CHECK(signbit(-1.0) && !signbit(1.0));
	CHECK(signbit(-HUGE_VAL) && !signbit(HUGE_VAL));
	CHECK(signbit(copysign(NAN, -1.0)) && !signbit(copysign(NAN, 1.0)));
}

/* ---- floor.html / ceil.html / trunc.html / round.html: NaN->NaN,
 * ±0/±Inf passed through unchanged, and "the result shall have the
 * same sign as x" for a zero result. ---- */
static void test_rounding(void)
{
	CHECK(isnan(floor(NAN)) && isnan(ceil(NAN)) && isnan(trunc(NAN)) && isnan(round(NAN)));
	CHECK(poszero(floor(0.0)) && negzero(floor(-0.0)));
	CHECK(poszero(ceil(0.0)) && negzero(ceil(-0.0)));
	CHECK(poszero(trunc(0.0)) && negzero(trunc(-0.0)));
	CHECK(poszero(round(0.0)) && negzero(round(-0.0)));
	CHECK(floor(HUGE_VAL) == HUGE_VAL && floor(-HUGE_VAL) == -HUGE_VAL);
	CHECK(ceil(HUGE_VAL) == HUGE_VAL && ceil(-HUGE_VAL) == -HUGE_VAL);
	CHECK(trunc(HUGE_VAL) == HUGE_VAL && trunc(-HUGE_VAL) == -HUGE_VAL);
	CHECK(round(HUGE_VAL) == HUGE_VAL && round(-HUGE_VAL) == -HUGE_VAL);

	/* the actual bug-prone cases: a mathematically-zero result whose
	 * sign must still track x, for inputs that are NOT themselves
	 * zero. ceil()/trunc() round a small negative fraction to zero;
	 * round() does too for anything with |x| < 0.5. */
	CHECK(negzero(ceil(-0.5)));
	CHECK(negzero(trunc(-0.5)));
	CHECK(negzero(round(-0.4)));
	CHECK(negzero(ceil(-0.9)));
	CHECK(negzero(trunc(-0.9)));
	/* floor() only returns a zero result for a zero input (any
	 * negative fraction floors to -1, not -0) -- sanity, not a
	 * distinct clause. */
	CHECK(floor(-0.5) == -1.0 && floor(-0.9) == -1.0);

	/* round(): "rounding halfway cases away from zero" */
	CHECK(round(2.5) == 3.0 && round(-2.5) == -3.0 && round(0.5) == 1.0 && round(-0.5) == -1.0);
}

/* ---- sqrt.html: RETURN VALUE / ERRORS -- domain error for x<0 or
 * x==-Inf (NaN), ±0 and +Inf passed through unchanged, NaN->NaN ---- */
static void test_sqrt(void)
{
	CHECK(isnan(sqrt(NAN)));
	CHECK(poszero(sqrt(0.0)) && negzero(sqrt(-0.0)));
	CHECK(sqrt(HUGE_VAL) == HUGE_VAL);
	CHECK(isnan(sqrt(-1.0)));       /* finite x < -0: domain error, NaN */
	CHECK(isnan(sqrt(-HUGE_VAL)));  /* x == -Inf: domain error, NaN */
	CHECK(isnan(sqrtl(-1.0L)) && isnan(sqrtf(-1.0f)));
}

/* ---- fmod.html: RETURN VALUE -- "x - i*y ... the result has the same
 * sign as x and magnitude less than the magnitude of y"; x or y NaN ->
 * NaN; y==0 or x==±Inf -> domain error, NaN; x==±0,y!=0 -> ±0;
 * x finite, y==±Inf -> x. ---- */
static void test_fmod(void)
{
	CHECK(fmod(7.5, 2.0) == 1.5);
	CHECK(fmod(-7.5, 2.0) == -1.5);   /* sign follows the dividend x, not y */
	CHECK(fmod(7.5, -2.0) == 1.5);
	CHECK(fmod(-7.5, -2.0) == -1.5);
	CHECK(isnan(fmod(NAN, 2.0)) && isnan(fmod(2.0, NAN)));
	CHECK(isnan(fmod(1.0, 0.0)) && isnan(fmod(-1.0, 0.0)));  /* y==0: domain error */
	CHECK(isnan(fmod(HUGE_VAL, 2.0)) && isnan(fmod(-HUGE_VAL, 2.0)));  /* x==±Inf: domain error */
	CHECK(poszero(fmod(0.0, 5.0)) && negzero(fmod(-0.0, 5.0)));
	/* x finite, y==±Inf: "x shall be returned" -- exactly, not just
	 * numerically equal (this is the clause the naive "reduce huge
	 * argument via fmod" trick in src/math/trig.c and src/math/pow.c
	 * relies on never mattering for finite y). */
	CHECK(fmod(3.5, HUGE_VAL) == 3.5 && fmod(-3.5, HUGE_VAL) == -3.5);
	CHECK(fmod(3.5, -HUGE_VAL) == 3.5);
	CHECK(fmod(HUGE_VAL, HUGE_VAL) != fmod(HUGE_VAL, HUGE_VAL));  /* still x==Inf -> NaN, not 3.5-style passthrough */
}

/* ---- frexp.html / ldexp.html / scalbn.html: round trips, ±0/NaN/±Inf
 * passthrough, exp==0 passthrough, overflow -> ±HUGE_VAL, underflow at
 * the denormal boundary. ---- */
static void test_frexp_ldexp(void)
{
	int e;
	double m;

	/* frexp: ±0 -> ±0, *exp==0 */
	m = frexp(0.0, &e); CHECK(poszero(m) && e == 0);
	m = frexp(-0.0, &e); CHECK(negzero(m) && e == 0);
	/* frexp: NaN -> NaN (value of *exp unspecified, not checked) */
	m = frexp(NAN, &e); CHECK(isnan(m));
	/* frexp: ±Inf -> x itself (value of *exp unspecified) */
	m = frexp(HUGE_VAL, &e); CHECK(m == HUGE_VAL);
	m = frexp(-HUGE_VAL, &e); CHECK(m == -HUGE_VAL);
	/* frexp: magnitude in [1/2, 1) for a normal, round-trips via
	 * scalbn -- exercised further into the denormal range than
	 * test/math.c's single 5e-324 case */
	m = frexp(DBL_MIN, &e); CHECK(m >= 0.5 && m < 1.0 && scalbn(m, e) == DBL_MIN);
	m = frexp(1e-310, &e); CHECK(m >= 0.5 && m < 1.0 && scalbn(m, e) == 1e-310);  /* subnormal input */

	/* ldexp/scalbn: exp==0 -> x unchanged, including for NaN/Inf/±0 */
	CHECK(ldexp(3.75, 0) == 3.75);
	CHECK(isnan(ldexp(NAN, 0)));
	CHECK(ldexp(HUGE_VAL, 0) == HUGE_VAL);
	CHECK(negzero(ldexp(-0.0, 0)));
	/* ldexp/scalbn: NaN -> NaN, ±0/±Inf -> x, for a nonzero exp too */
	CHECK(isnan(ldexp(NAN, 5)));
	CHECK(poszero(ldexp(0.0, 5)) && negzero(ldexp(-0.0, 5)));
	CHECK(ldexp(HUGE_VAL, 5) == HUGE_VAL && ldexp(-HUGE_VAL, 5) == -HUGE_VAL);
	/* ldexp/scalbn: overflow -> ±HUGE_VAL "according to the sign of x" */
	CHECK(ldexp(DBL_MAX, 1) == HUGE_VAL);
	CHECK(ldexp(-DBL_MAX, 1) == -HUGE_VAL);
	CHECK(scalbn(1.0, 2000) == HUGE_VAL && scalbn(-1.0, 2000) == -HUGE_VAL);
	/* underflow past the smallest subnormal: representable range is
	 * exhausted, so the correct result rounds to a signed zero */
	CHECK(negzero(scalbn(-1.0, -2000)));
	CHECK(poszero(scalbn(1.0, -2000)));
}

/* ---- modf.html: RETURN VALUE -- signed fractional part; NaN -> NaN
 * with *iptr NaN; ±Inf -> ±0 with *iptr ±Inf. ---- */
static void test_modf(void)
{
	double ip, fr;

	fr = modf(NAN, &ip); CHECK(isnan(fr) && isnan(ip));
	fr = modf(HUGE_VAL, &ip); CHECK(poszero(fr) && ip == HUGE_VAL);
	fr = modf(-HUGE_VAL, &ip); CHECK(negzero(fr) && ip == -HUGE_VAL);
	/* sign-of-zero-result case, same shape as ceil/trunc/round above:
	 * the integral part of a small negative fraction is -0, not +0 */
	fr = modf(-0.5, &ip); CHECK(negzero(ip) && fr == -0.5);
	fr = modf(-0.0, &ip); CHECK(negzero(ip) && negzero(fr));
}

/* ---- exp.html: RETURN VALUE -- NaN->NaN, ±0->1, -Inf->+0, +Inf->x;
 * overflow -> HUGE_VAL. ---- */
static void test_exp(void)
{
	CHECK(isnan(exp(NAN)));
	CHECK(exp(0.0) == 1.0 && exp(-0.0) == 1.0);
	CHECK(poszero(exp(-HUGE_VAL)));
	CHECK(exp(HUGE_VAL) == HUGE_VAL);
	CHECK(exp(1000.0) == HUGE_VAL);   /* overflow -> HUGE_VAL, informational: exact threshold not asserted */
	CHECK(poszero(exp(-1000.0)));     /* underflow: 0.0 permitted (IEC 60559 branch) */
}

/* ---- log.html / log2.html / log10.html: RETURN VALUE/ERRORS -- NaN->
 * NaN, x==1->+0, x==+Inf->+Inf (x itself), x==±0-> pole error, -HUGE_VAL,
 * finite x<0 or x==-Inf -> domain error, NaN. ---- */
static void test_log(void)
{
	CHECK(isnan(log(NAN)) && isnan(log2(NAN)) && isnan(log10(NAN)));
	CHECK(poszero(log(1.0)) && poszero(log2(1.0)) && poszero(log10(1.0)));
	CHECK(log(HUGE_VAL) == HUGE_VAL && log2(HUGE_VAL) == HUGE_VAL && log10(HUGE_VAL) == HUGE_VAL);
	/* pole error: ±0 -> -HUGE_VAL (both signs of zero alike -- the
	 * clause does not distinguish +0 from -0 here) */
	CHECK(log(0.0) == -HUGE_VAL && log(-0.0) == -HUGE_VAL);
	CHECK(log2(0.0) == -HUGE_VAL && log10(0.0) == -HUGE_VAL);
	/* domain error: finite negative, and -Inf */
	CHECK(isnan(log(-1.0)) && isnan(log(-HUGE_VAL)));
	CHECK(isnan(log2(-1.0)) && isnan(log10(-1.0)));
}

/* ---- sin.html / cos.html / tan.html: NaN->NaN, ±0->x (sin/tan) or
 * ->1 (cos), ±Inf -> domain error, NaN.
 * atan.html: NaN->NaN, ±0->x, ±Inf->±pi/2.
 * atan2.html: the full sign/zero/Inf table. ---- */
static void test_trig(void)
{
	CHECK(isnan(sin(NAN)) && isnan(cos(NAN)) && isnan(tan(NAN)) && isnan(atan(NAN)));
	CHECK(poszero(sin(0.0)) && negzero(sin(-0.0)));
	CHECK(cos(0.0) == 1.0 && cos(-0.0) == 1.0);
	CHECK(poszero(tan(0.0)) && negzero(tan(-0.0)));
	CHECK(poszero(atan(0.0)) && negzero(atan(-0.0)));
	CHECK(isnan(sin(HUGE_VAL)) && isnan(sin(-HUGE_VAL)));
	CHECK(isnan(cos(HUGE_VAL)) && isnan(cos(-HUGE_VAL)));
	CHECK(isnan(tan(HUGE_VAL)) && isnan(tan(-HUGE_VAL)));
	CHECK(atan(HUGE_VAL) == M_PI / 2 && atan(-HUGE_VAL) == -M_PI / 2);

	CHECK(isnan(atan2(NAN, 1.0)) && isnan(atan2(1.0, NAN)));
	CHECK(poszero(atan2(0.0, 1.0)) && negzero(atan2(-0.0, 1.0)));  /* y==±0, x>0 -> ±0 */
	CHECK(atan2(0.0, -1.0) == M_PI && atan2(-0.0, -1.0) == -M_PI); /* y==±0, x<0 -> ±pi */
	CHECK(atan2(1.0, 0.0) == M_PI / 2 && atan2(-1.0, 0.0) == -M_PI / 2);  /* y!=0, x==+0 */
	CHECK(atan2(1.0, -0.0) == M_PI / 2 && atan2(-1.0, -0.0) == -M_PI / 2);  /* y!=0, x==-0: same as x==+0 */
	CHECK(atan2(HUGE_VAL, 1.0) == M_PI / 2 && atan2(-HUGE_VAL, 1.0) == -M_PI / 2);  /* finite x, y==±Inf */
	CHECK(atan2(1.0, HUGE_VAL) == 0.0 && atan2(1.0, -HUGE_VAL) == M_PI);           /* finite y>0, x==±Inf */
	CHECK(atan2(HUGE_VAL, HUGE_VAL) == M_PI / 4);      /* y==+Inf, x==+Inf -> +pi/4 */
	CHECK(atan2(HUGE_VAL, -HUGE_VAL) == 3 * M_PI / 4);  /* y==+Inf, x==-Inf -> +3pi/4 */
	CHECK(atan2(-HUGE_VAL, HUGE_VAL) == -M_PI / 4);
	CHECK(atan2(-HUGE_VAL, -HUGE_VAL) == -3 * M_PI / 4);
}

/* ---- pow.html: RETURN VALUE/ERRORS -- the full special-value table,
 * worked clause by clause rather than sampled. ---- */
static void test_pow(void)
{
	/* "For any value of x (including NaN), if y is ±0, 1.0" */
	CHECK(pow(2.0, 0.0) == 1.0 && pow(2.0, -0.0) == 1.0 && pow(NAN, 0.0) == 1.0);
	/* "For any value of y (including NaN), if x is +1, 1.0" */
	CHECK(pow(1.0, 5.0) == 1.0 && pow(1.0, NAN) == 1.0 && pow(1.0, HUGE_VAL) == 1.0);
	/* "If x is -1, and y is ±Inf, 1.0" */
	CHECK(pow(-1.0, HUGE_VAL) == 1.0 && pow(-1.0, -HUGE_VAL) == 1.0);
	/* domain error: finite x<0, finite non-integer y -> NaN */
	CHECK(isnan(pow(-2.0, 0.5)));
	/* x or y NaN (not otherwise specified) -> NaN */
	CHECK(isnan(pow(NAN, 2.0)) && isnan(pow(-1.0, NAN)));

	/* x==±0, y odd integer >0 -> ±0; y>0 not odd integer -> +0 */
	CHECK(poszero(pow(0.0, 3.0)) && negzero(pow(-0.0, 3.0)));
	CHECK(poszero(pow(0.0, 4.0)) && poszero(pow(-0.0, 4.0)));
	/* x==±0, y<0: pole error, odd integer -> ±HUGE_VAL, else +HUGE_VAL */
	CHECK(pow(0.0, -3.0) == HUGE_VAL && pow(-0.0, -3.0) == -HUGE_VAL);
	CHECK(pow(0.0, -4.0) == HUGE_VAL && pow(-0.0, -4.0) == HUGE_VAL);

	/* |x|<1, y==-Inf -> +Inf; |x|>1, y==-Inf -> +0 */
	CHECK(pow(0.5, -HUGE_VAL) == HUGE_VAL);
	CHECK(poszero(pow(2.0, -HUGE_VAL)));
	/* |x|<1, y==+Inf -> +0; |x|>1, y==+Inf -> +Inf */
	CHECK(poszero(pow(0.5, HUGE_VAL)));
	CHECK(pow(2.0, HUGE_VAL) == HUGE_VAL);

	/* x==-Inf: y odd int<0 -> -0; y<0 not odd -> +0;
	 *          y odd int>0 -> -Inf; y>0 not odd -> +Inf */
	CHECK(negzero(pow(-HUGE_VAL, -3.0)));
	CHECK(poszero(pow(-HUGE_VAL, -4.0)));
	CHECK(pow(-HUGE_VAL, 3.0) == -HUGE_VAL);
	CHECK(pow(-HUGE_VAL, 4.0) == HUGE_VAL);
	/* x==+Inf: y<0 -> +0, y>0 -> +Inf */
	CHECK(poszero(pow(HUGE_VAL, -2.0)));
	CHECK(pow(HUGE_VAL, 3.0) == HUGE_VAL);

	/* overflow/underflow far from any special value: informational
	 * sign/magnitude sanity, not a distinct clause beyond RETURN
	 * VALUE's overflow->HUGE_VAL text already exercised above */
	CHECK(pow(2.0, 2000.0) == HUGE_VAL && poszero(pow(2.0, -2000.0)));
}

/* ---- fmax.html / fmin.html: RETURN VALUE -- one-NaN-argument returns
 * the other; both-NaN returns NaN. ---- */
static void test_fmaxmin(void)
{
	CHECK(fmax(NAN, 3.0) == 3.0 && fmax(3.0, NAN) == 3.0);
	CHECK(fmin(NAN, 3.0) == 3.0 && fmin(3.0, NAN) == 3.0);
	CHECK(isnan(fmax(NAN, NAN)) && isnan(fmin(NAN, NAN)));
	CHECK(fmax(1.0, 2.0) == 2.0 && fmin(1.0, 2.0) == 1.0);
	/* informational only: POSIX does not specify which zero wins when
	 * comparing +0 and -0 (they compare equal), so this pins down
	 * ntlibc's own (permitted) choice rather than a "shall" clause --
	 * see src/math/fmax.c's "+0 beats -0" comment. */
	CHECK(poszero(fmax(0.0, -0.0)) && poszero(fmax(-0.0, 0.0)));
	CHECK(negzero(fmin(0.0, -0.0)) && negzero(fmin(-0.0, 0.0)));
}

/* ---- hypot.html: RETURN VALUE -- ±Inf wins even over a NaN co-argument
 * (either order); a non-Inf NaN argument makes the result NaN; overflow
 * -> HUGE_VAL. ---- */
static void test_hypot(void)
{
	CHECK(hypot(HUGE_VAL, NAN) == HUGE_VAL);
	CHECK(hypot(NAN, HUGE_VAL) == HUGE_VAL);   /* symmetric in the arguments */
	CHECK(hypot(-HUGE_VAL, NAN) == HUGE_VAL);
	CHECK(isnan(hypot(NAN, 1.0)) && isnan(hypot(1.0, NAN)));
	CHECK(hypot(3.0, 4.0) == 5.0);
	CHECK(hypot(DBL_MAX, DBL_MAX) == HUGE_VAL);  /* overflow -> HUGE_VAL */

	/* hypot.html RETURN VALUE -- "If x or y is ±Inf, +Inf shall be
	 * returned (even if one of x or y is NaN)."  Both arguments
	 * infinite, neither a NaN: the infinity check must run before (not
	 * only alongside) the NaN check. */
	CHECK(hypot(HUGE_VAL, HUGE_VAL) == HUGE_VAL);
	CHECK(hypot(-HUGE_VAL, -HUGE_VAL) == HUGE_VAL);
	CHECK(hypot(HUGE_VAL, -HUGE_VAL) == HUGE_VAL);
}

/* ---- nan.html: RETURN VALUE -- "a quiet NaN, if available". ---- */
static void test_nan(void)
{
	double d = nan("");
	CHECK(isnan(d));
	CHECK(d != d);   /* quiet NaN still compares unordered, per IEEE 754 */
	CHECK(isnan(nanf("")) && isnan(nanl("")));
}

/* ---- math.h basedefs, math_errhandling: "The following macros shall
 * expand to the integer constants 1 and 2, respectively; MATH_ERRNO
 * MATH_ERREXCEPT" and "If the expression (math_errhandling &
 * MATH_ERREXCEPT) can be non-zero, the implementation shall define the
 * macros FE_DIVBYZERO, FE_INVALID, and FE_OVERFLOW in <fenv.h>." ---- */
static void test_errhandling(void)
{
	volatile double big, tiny, zero, three, negone, result;
	CHECK(MATH_ERRNO == 1 && MATH_ERREXCEPT == 2);
	CHECK(math_errhandling == MATH_ERREXCEPT);

	/* <fenv.h> basedefs: FE_DIVBYZERO, FE_INVALID and FE_OVERFLOW (the
	 * three basedefs/math.h.html names) must exist with these exact
	 * values -- the traditional x86 status-word bit positions, which
	 * is also what feclearexcept()/fetestexcept() below observe on the
	 * real hardware flags. */
	CHECK(FE_INVALID == 0x01 && FE_DIVBYZERO == 0x04 && FE_OVERFLOW == 0x08);
	CHECK(FE_UNDERFLOW == 0x10 && FE_INEXACT == 0x20);
	CHECK(FE_ALL_EXCEPT == (FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT));

	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	CHECK(fetestexcept(FE_ALL_EXCEPT) == 0);

	/* sqrt.html ERRORS: "a domain error occurs" for sqrt(x) with x<0.
	 * -1.0 goes straight to hardware fsqrt (src/math/sqrt.c), which
	 * signals invalid for a negative operand on both arches. */
	negone = -1.0;
	(void)sqrt(negone);
	CHECK(fetestexcept(FE_INVALID) == FE_INVALID);
	CHECK(feclearexcept(FE_INVALID) == 0 && fetestexcept(FE_ALL_EXCEPT) == 0);

	/* log.html ERRORS: "a pole error may occur" for log(+-0); fyl2x
	 * signals divide-by-zero for a zero operand (src/math/log.c). */
	zero = 0.0;
	(void)log(zero);
	CHECK(fetestexcept(FE_DIVBYZERO) == FE_DIVBYZERO);
	CHECK(feclearexcept(FE_DIVBYZERO) == 0 && fetestexcept(FE_ALL_EXCEPT) == 0);

	/* DBL_MAX*DBL_MAX overflows -- this is plain compiler-emitted `*`,
	 * not a src/math/x87.h helper, so on x86_64 it exercises the SSE2
	 * mulsd path (MXCSR) rather than x87; on i386 tcc still emits x87
	 * fmul for `double`.  Either way fetestexcept() must see it.  The
	 * result must actually land in a real `double` (not be discarded):
	 * x87's registers are 80-bit, so overflow relative to *double*'s
	 * narrower exponent range is only detected at the store that
	 * rounds/converts down to 64 bits -- a discarded `(void)(a*b)` can
	 * pop the x87 stack without ever performing that store. */
	big = DBL_MAX;
	result = big * big;
	/* An overflowing result is necessarily also inexact (the true
	 * mathematical product cannot be represented at all), so this
	 * checks only that FE_OVERFLOW is among the raised flags, not
	 * that it is the only one -- then clears everything before the
	 * next case. */
	CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_OVERFLOW) == FE_OVERFLOW);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0 && fetestexcept(FE_ALL_EXCEPT) == 0);

	/* A tiny compiler-emitted division that rounds to a subnormal
	 * result signals underflow (and inexact alongside it, same
	 * both-flags-raised-together reasoning as overflow above); same
	 * store-to-a-real-double requirement as above. */
	tiny = DBL_MIN;
	big = 1e16;
	result = tiny / big;
	CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_UNDERFLOW) == FE_UNDERFLOW);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0 && fetestexcept(FE_ALL_EXCEPT) == 0);

	/* 1.0/3.0 is not exactly representable -- signals inexact, and
	 * nothing else. */
	three = 3.0;
	result = 1.0 / three;
	CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_INEXACT);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0 && fetestexcept(FE_ALL_EXCEPT) == 0);
	(void)result;

	/* feraiseexcept()/fesetexceptflag() can set flags directly,
	 * independent of any actual computation. */
	CHECK(feraiseexcept(FE_INVALID | FE_OVERFLOW) == 0);
	CHECK(fetestexcept(FE_ALL_EXCEPT) == (FE_INVALID | FE_OVERFLOW));
	{
		fexcept_t saved;
		CHECK(fegetexceptflag(&saved, FE_ALL_EXCEPT) == 0);
		CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
		CHECK(fetestexcept(FE_ALL_EXCEPT) == 0);
		CHECK(fesetexceptflag(&saved, FE_ALL_EXCEPT) == 0);
		CHECK(fetestexcept(FE_ALL_EXCEPT) == (FE_INVALID | FE_OVERFLOW));
	}
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);

	/* fegetround()/fesetround(): default is round-to-nearest; setting
	 * and restoring another mode round-trips. */
	CHECK(fegetround() == FE_TONEAREST);
	CHECK(fesetround(FE_TOWARDZERO) == 0);
	CHECK(fegetround() == FE_TOWARDZERO);
	CHECK(fesetround(FE_TONEAREST) == 0);
	CHECK(fegetround() == FE_TONEAREST);
	CHECK(fesetround(0xdead) == -1);  /* not one of the four modes */

	/* fegetenv()/fesetenv()/feholdexcept()/feupdateenv(): a
	 * saved-and-restored environment round-trips the exception state
	 * exactly as the C99 feupdateenv() contract requires -- raised
	 * exceptions accumulated *during* the held region are merged back
	 * in on top of whatever the caller had pending before. */
	{
		fenv_t env;
		CHECK(feholdexcept(&env) == 0);
		CHECK(fetestexcept(FE_ALL_EXCEPT) == 0);
		zero = 0.0;
		(void)log(zero);  /* raises FE_DIVBYZERO while "held" */
		CHECK(fetestexcept(FE_DIVBYZERO) == FE_DIVBYZERO);
		CHECK(feupdateenv(&env) == 0);
		CHECK(fetestexcept(FE_DIVBYZERO) == FE_DIVBYZERO);
		CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	}

	/* Since MATH_ERRNO is (correctly, per math_errhandling's value)
	 * not required, confirm errno is in fact left alone across a
	 * representative set of domain/pole/range "errors" -- this is
	 * informational (documents actual, conformant-either-way
	 * behaviour), not a distinct "shall" clause. */
	errno = 0;
	(void)sqrt(-1.0);
	(void)log(0.0);
	(void)log(-1.0);
	(void)pow(0.0, -1.0);
	(void)fmod(1.0, 0.0);
	(void)exp(1000.0);
	CHECK(errno == 0);
}

int main(void)
{
	test_fabs();
	test_copysign();
	test_classify();
	test_rounding();
	test_sqrt();
	test_fmod();
	test_frexp_ldexp();
	test_modf();
	test_exp();
	test_log();
	test_trig();
	test_pow();
	test_fmaxmin();
	test_hypot();
	test_nan();
	test_errhandling();

	if (!fails) printf("posix-math: all tests passed\n");
	return fails != 0;
}
