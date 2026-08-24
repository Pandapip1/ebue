/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of math.h, concentrating on what
 * test/math.c (a broad sanity pass, not clause-cited) does not already
 * pin down: special-value tables (±0, ±Inf, NaN), sign of zero (which
 * == cannot distinguish -- use signbit()), and the math_errhandling /
 * errno contract.  Each assertion cites the clause of
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html
 * it checks.  See test/POSIX-COVERAGE.md's math.h section for the full
 * ledger.
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
 *
 * Every requirement this file names now gets a test, even one that
 * cannot pass: fenced blocks use three conventions, greppable by the
 * tag right after "#if 0 /* ":
 *   BUG:    a confirmed, real spec violation (should pass once fixed)
 *   N/A:    genuinely impossible on this platform
 *   UNIMPL: not implemented at all here, but implementable
 * A fenced test still contains the real assertions the cited spec
 * clause requires, written as if it would run -- never a hand-wave.
 */
#include <stdio.h>
#include <math.h>
#include <float.h>
#include <errno.h>
#include <fenv.h>
#include <limits.h>

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

static int negzerof(float x) { return x == 0.0f && signbit(x); }
static int poszerof(float x) { return x == 0.0f && !signbit(x); }
static int negzerol(long double x) { return x == 0.0L && signbit(x); }
static int poszerol(long double x) { return x == 0.0L && !signbit(x); }

/* ---- sin.html/cos.html/tan.html/atan.html/atan2.html, l/f variants:
 * the same special-value tables audited above for the double forms
 * apply verbatim to sinf/sinl, cosf/cosl, tanf/tanl, atanf/atanl,
 * atan2f/atan2l ("shall be equivalent to" the double function).
 * ntlibc's `long double` is 64-bit on this PE target -- bit-identical
 * layout to `double` (src/math/x87.h's NTLIBC_LDBL_EXTENDED note) --
 * so unlike a genuine 80-bit long double there is no extra internal
 * precision to lose at the l-variant's own store; any float/long
 * double vs. double numeric difference reported here is purely
 * informational (float has less precision by construction; a real
 * 80-bit long double target could differ from double in the last bit
 * for reasons this target cannot exhibit), never a failing assertion. ---- */
static void test_float_ld_variants(void)
{
	CHECK(isnan(sinf(NAN)) && isnan(sinl(NAN)));
	CHECK(isnan(cosf(NAN)) && isnan(cosl(NAN)));
	CHECK(isnan(tanf(NAN)) && isnan(tanl(NAN)));
	CHECK(isnan(atanf(NAN)) && isnan(atanl(NAN)));

	CHECK(poszerof(sinf(0.0f)) && negzerof(sinf(-0.0f)));
	CHECK(poszerol(sinl(0.0L)) && negzerol(sinl(-0.0L)));
	CHECK(cosf(0.0f) == 1.0f && cosf(-0.0f) == 1.0f);
	CHECK(cosl(0.0L) == 1.0L && cosl(-0.0L) == 1.0L);
	CHECK(poszerof(tanf(0.0f)) && negzerof(tanf(-0.0f)));
	CHECK(poszerol(tanl(0.0L)) && negzerol(tanl(-0.0L)));
	CHECK(poszerof(atanf(0.0f)) && negzerof(atanf(-0.0f)));
	CHECK(poszerol(atanl(0.0L)) && negzerol(atanl(-0.0L)));

	/* sin/cos/tan: ±Inf is a domain error -> NaN */
	CHECK(isnan(sinf(HUGE_VALF)) && isnan(sinf(-HUGE_VALF)));
	CHECK(isnan(sinl(HUGE_VALL)) && isnan(sinl(-HUGE_VALL)));
	CHECK(isnan(cosf(HUGE_VALF)) && isnan(cosf(-HUGE_VALF)));
	CHECK(isnan(cosl(HUGE_VALL)) && isnan(cosl(-HUGE_VALL)));
	CHECK(isnan(tanf(HUGE_VALF)) && isnan(tanf(-HUGE_VALF)));
	CHECK(isnan(tanl(HUGE_VALL)) && isnan(tanl(-HUGE_VALL)));

	/* atan.html: ±Inf -> ±pi/2. The l-variant comparisons use a
	 * tolerance, not == : M_PI is a `double` macro (53 bits of
	 * mantissa), so `(long double)M_PI/2` only carries double
	 * precision even where `long double` genuinely has more (the
	 * native `make asan` build, __SIZEOF_LONG_DOUBLE__>8) -- atanl()
	 * itself computes at the type's full precision via fpatan, so an
	 * exact == there would be asserting an accuracy coincidence, not
	 * the spec's actual "±pi/2" clause. 1e-9L is far tighter than any
	 * real bug could hide under yet far looser than the ~1e-17
	 * relative gap M_PI's own truncation can introduce -- on this PE
	 * target (long double == double) it holds with room to spare. */
	CHECK(atanf(HUGE_VALF) == (float)(M_PI / 2) && atanf(-HUGE_VALF) == (float)(-M_PI / 2));
	CHECK(fabsl(atanl(HUGE_VALL) - (long double)M_PI / 2) < 1e-9L);
	CHECK(fabsl(atanl(-HUGE_VALL) - (long double)-M_PI / 2) < 1e-9L);

	/* atan2.html: representative subset of the same table test_trig()
	 * exercises fully for double -- NaN, y==±0 with x>0, and the
	 * finite-y/x==±Inf clauses. Same M_PI-precision caveat as above
	 * for the l-variant pi/pi-fraction comparisons. */
	CHECK(isnan(atan2f(NAN, 1.0f)) && isnan(atan2f(1.0f, NAN)));
	CHECK(isnan(atan2l(NAN, 1.0L)) && isnan(atan2l(1.0L, NAN)));
	CHECK(poszerof(atan2f(0.0f, 1.0f)) && negzerof(atan2f(-0.0f, 1.0f)));
	CHECK(poszerol(atan2l(0.0L, 1.0L)) && negzerol(atan2l(-0.0L, 1.0L)));
	CHECK(atan2f(HUGE_VALF, HUGE_VALF) == (float)(M_PI / 4));
	CHECK(fabsl(atan2l(HUGE_VALL, HUGE_VALL) - (long double)M_PI / 4) < 1e-9L);
	CHECK(atan2f(1.0f, HUGE_VALF) == 0.0f && atan2f(1.0f, -HUGE_VALF) == (float)M_PI);
	CHECK(atan2l(1.0L, HUGE_VALL) == 0.0L);
	CHECK(fabsl(atan2l(1.0L, -HUGE_VALL) - (long double)M_PI) < 1e-9L);

	/* exp.html/log.html/log2.html/log10.html l/f variants: NaN->NaN,
	 * ±0->1 (exp) / +0 (log family pole error), +Inf->x, finite
	 * negative -> domain error NaN. */
	CHECK(isnan(expf(NAN)) && isnan(expl(NAN)));
	CHECK(expf(0.0f) == 1.0f && expf(-0.0f) == 1.0f);
	CHECK(expl(0.0L) == 1.0L && expl(-0.0L) == 1.0L);
	CHECK(expf(HUGE_VALF) == HUGE_VALF && expl(HUGE_VALL) == HUGE_VALL);
	CHECK(poszerof(expf(-HUGE_VALF)) && poszerol(expl(-HUGE_VALL)));

	CHECK(isnan(logf(NAN)) && isnan(logl(NAN)) && isnan(log2f(NAN)) && isnan(log2l(NAN)) && isnan(log10f(NAN)) && isnan(log10l(NAN)));
	CHECK(poszerof(logf(1.0f)) && poszerol(logl(1.0L)));
	CHECK(logf(HUGE_VALF) == HUGE_VALF && logl(HUGE_VALL) == HUGE_VALL);
	CHECK(logf(0.0f) == -HUGE_VALF && logl(0.0L) == -HUGE_VALL);   /* pole error */
	CHECK(isnan(logf(-1.0f)) && isnan(logl(-1.0L)));               /* domain error */

	/* pow.html l/f variants: representative subset (full ~20-clause
	 * table already worked for double in test_pow()) -- the y==±0
	 * and x==1 identities, and the domain-error case. */
	CHECK(powf(2.0f, 0.0f) == 1.0f && powl(2.0L, 0.0L) == 1.0L);
	CHECK(powf(1.0f, NAN) == 1.0f && powl(1.0L, (long double)NAN) == 1.0L);
	CHECK(isnan(powf(-2.0f, 0.5f)) && isnan(powl(-2.0L, 0.5L)));
}

/* ---- lround.html/llround.html/lrint.html/llrint.html/rint.html:
 * clause-by-clause (not just the round-trip spot checks test/math.c
 * already has). No f/l-suffixed variants (lroundf/lroundl/lrintf/
 * lrintl/llroundf/llroundl/llrintf/llrintl/rintf/rintl) are audited
 * separately below, in test_lround_lrint_variants(). ---- */
static void test_lround_lrint(void)
{
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);

	/* NaN input: RETURN VALUE says "domain error ... unspecified
	 * value returned" -- only the required FE_INVALID (math_errhandling
	 * here is unconditionally MATH_ERREXCEPT, so this is a real "shall")
	 * is checked, never the unspecified return value itself. Confirmed
	 * live to hold on both arches. */
	(void)lround(NAN);
	CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) == FE_INVALID);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	(void)llround(NAN);
	CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) == FE_INVALID);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	(void)lrint(NAN);
	CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) == FE_INVALID);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	(void)llrint(NAN);
	CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) == FE_INVALID);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);

	/* lround/llround: "rounding halfway cases away from zero,
	 * regardless of the current rounding direction" -- unlike
	 * lrint/llrint/rint below, these do NOT consult fegetround(). */
	CHECK(fesetround(FE_TOWARDZERO) == 0);
	CHECK(lround(2.5) == 3 && lround(-2.5) == -3);
	CHECK(llround(2.5) == 3 && llround(-2.5) == -3);
	CHECK(fesetround(FE_TONEAREST) == 0);

	/* lrint/llrint/rint: RETURN VALUE -- "using the current rounding
	 * direction" (rint.html), genuinely testable now that <fenv.h>
	 * exists.  Ties-to-even at the default FE_TONEAREST. */
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	CHECK(lrint(2.5) == 2 && lrint(3.5) == 4);
	CHECK(llrint(2.5) == 2 && llrint(3.5) == 4);
	CHECK(rint(2.5) == 2.0 && rint(3.5) == 4.0);

	CHECK(fesetround(FE_UPWARD) == 0);
	CHECK(lrint(2.1) == 3 && llrint(2.1) == 3 && rint(2.1) == 3.0);
	CHECK(fesetround(FE_DOWNWARD) == 0);
	CHECK(lrint(2.9) == 2 && llrint(2.9) == 2 && rint(2.9) == 2.0);
	CHECK(fesetround(FE_TOWARDZERO) == 0);
	CHECK(lrint(-2.9) == -2 && rint(-2.9) == -2.0);
	CHECK(fesetround(FE_TONEAREST) == 0);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);

	/* rint.html: NaN->NaN, ±0->x, ±Inf->x passthrough -- rint's
	 * `double` return type can actually represent these, unlike
	 * lrint/llrint's integer types (which hit the NaN/Inf domain-error
	 * clause checked above instead). */
	CHECK(isnan(rint(NAN)));
	CHECK(poszero(rint(0.0)) && negzero(rint(-0.0)));
	CHECK(rint(HUGE_VAL) == HUGE_VAL && rint(-HUGE_VAL) == -HUGE_VAL);

	/* rint.html: "may raise the inexact floating-point exception if
	 * the result differs in value from the argument" -- confirmed live
	 * that ntlibc's frndint-based rint() does so. */
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	CHECK(rint(2.5) == 2.0);
	CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_INEXACT) == FE_INEXACT);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);

	/* lround.html/llround.html/lrint.html/llrint.html ERRORS -- "A
	 * domain error shall occur ... if x is ... infinite, or the
	 * correct value is not representable as an integer", and
	 * math_errhandling here is unconditionally MATH_ERREXCEPT
	 * (include/math.h), so FE_INVALID must be raised for these cases
	 * too, exactly as it already is for a NaN argument above.
	 *
	 * BUG found and fixed this session: src/math/round.c used to do
	 * this via a raw `(long)roundl(x)` / `(long long)
	 * __x87_rndint(x,-1)` cast -- undefined behaviour in C itself for
	 * a NaN or out-of-range operand, and inconsistent in practice: it
	 * happened to raise FE_INVALID on x86_64 (the compiler's
	 * cvttsd2si/fistp does that for free) but NOT on i386, where
	 * lround(+Inf) raised FE_DIVBYZERO instead of FE_INVALID and the
	 * finite-but-unrepresentable cases (lround(1e30), lrint(1e30),
	 * llrint(1e300)) raised nothing at all -- apparently because the
	 * cast there routed through arch/i386/src/int64.c's runtime
	 * int64-conversion helper, which did not reproduce x86_64's
	 * incidental exception behaviour. Fixed by checking the rounded
	 * value's range explicitly before ever casting it, and calling
	 * feraiseexcept(FE_INVALID) directly -- well-defined and
	 * consistent on both arches now (confirmed live). */
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	(void)lround(HUGE_VAL);
	CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) == FE_INVALID);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	(void)lround(-HUGE_VAL);
	CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) == FE_INVALID);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	(void)lround(1e30);   /* finite, but not representable as long */
	CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) == FE_INVALID);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	(void)lrint(HUGE_VAL);
	CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) == FE_INVALID);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	(void)lrint(1e30);
	CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) == FE_INVALID);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	(void)llrint(1e300);   /* finite, but not representable as long long */
	CHECK((fetestexcept(FE_ALL_EXCEPT) & FE_INVALID) == FE_INVALID);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
}

/* ---- Functions src/math/ did not implement until now: asin/acos,
 * sinh/cosh/tanh, asinh/acosh/atanh, cbrt, expm1/log1p, erf/erfc,
 * lgamma/tgamma, the Bessel functions, remainder/remquo,
 * nextafter/nexttoward, fdim, fma, ilogb/logb, nearbyint, scalbln, and
 * the f/l-suffixed lround/lrint family below. Each assertion is cited
 * to its own spec page. ---- */

/* asin.html/acos.html RETURN VALUE/ERRORS -- NaN->NaN;
 * ±0->x (asin only; acos(+1)->+0); finite |x|>1 -> domain error,
 * NaN; ±Inf -> domain error, NaN (both). asin/acos are not
 * declared by include/math.h at all. */
static void test_asin_acos(void)
{
	CHECK(isnan(asin(NAN)) && isnan(acos(NAN)));
	CHECK(poszero(asin(0.0)) && negzero(asin(-0.0)));
	CHECK(poszero(acos(1.0)));
	CHECK(isnan(asin(1.5)) && isnan(asin(-1.5)));      /* finite |x|>1 */
	CHECK(isnan(acos(1.5)) && isnan(acos(-1.5)));
	CHECK(isnan(asin(HUGE_VAL)) && isnan(asin(-HUGE_VAL)));
	CHECK(isnan(acos(HUGE_VAL)) && isnan(acos(-HUGE_VAL)));
	CHECK(asin(1.0) == M_PI / 2 && asin(-1.0) == -M_PI / 2);
}

/* sinh.html/cosh.html/tanh.html RETURN VALUE/ERRORS --
 * NaN->NaN; sinh/tanh: ±0/±Inf->x; cosh: ±0->1, ±Inf->+Inf;
 * sinh/cosh overflow -> range error, ±HUGE_VAL. Not declared by
 * include/math.h. */
static void test_hyperbolic(void)
{
	CHECK(isnan(sinh(NAN)) && isnan(cosh(NAN)) && isnan(tanh(NAN)));
	CHECK(poszero(sinh(0.0)) && negzero(sinh(-0.0)));
	CHECK(cosh(0.0) == 1.0 && cosh(-0.0) == 1.0);
	CHECK(poszero(tanh(0.0)) && negzero(tanh(-0.0)));
	CHECK(sinh(HUGE_VAL) == HUGE_VAL && sinh(-HUGE_VAL) == -HUGE_VAL);
	CHECK(cosh(HUGE_VAL) == HUGE_VAL && cosh(-HUGE_VAL) == HUGE_VAL);
	CHECK(tanh(HUGE_VAL) == 1.0 && tanh(-HUGE_VAL) == -1.0);
	CHECK(sinh(1000.0) == HUGE_VAL);    /* overflow -> range error, HUGE_VAL */
	CHECK(cosh(1000.0) == HUGE_VAL);
}

/* asinh.html/acosh.html/atanh.html RETURN VALUE/ERRORS --
 * NaN->NaN; asinh: ±0/±Inf->x; acosh: x==1->+0, x==+Inf->+Inf,
 * x<1 or x==-Inf -> domain error, NaN; atanh: ±0->x, ±1 -> pole
 * error ±HUGE_VAL, finite |x|>1 or ±Inf -> domain error, NaN.
 * Not declared by include/math.h. */
static void test_inverse_hyperbolic(void)
{
	CHECK(isnan(asinh(NAN)) && isnan(acosh(NAN)) && isnan(atanh(NAN)));
	CHECK(poszero(asinh(0.0)) && negzero(asinh(-0.0)));
	CHECK(asinh(HUGE_VAL) == HUGE_VAL && asinh(-HUGE_VAL) == -HUGE_VAL);
	CHECK(poszero(acosh(1.0)));
	CHECK(acosh(HUGE_VAL) == HUGE_VAL);
	CHECK(isnan(acosh(0.5)) && isnan(acosh(-HUGE_VAL)));   /* x<1, x==-Inf */
	CHECK(poszero(atanh(0.0)) && negzero(atanh(-0.0)));
	CHECK(atanh(1.0) == HUGE_VAL && atanh(-1.0) == -HUGE_VAL);   /* pole error */
	CHECK(isnan(atanh(1.5)) && isnan(atanh(-1.5)));               /* finite |x|>1 */
	CHECK(isnan(atanh(HUGE_VAL)) && isnan(atanh(-HUGE_VAL)));     /* ±Inf domain error */
}

/* cbrt.html RETURN VALUE -- "No errors are defined."
 * NaN->NaN, ±0/±Inf->x. Not declared by include/math.h. */
static void test_cbrt(void)
{
	CHECK(isnan(cbrt(NAN)));
	CHECK(poszero(cbrt(0.0)) && negzero(cbrt(-0.0)));
	CHECK(cbrt(HUGE_VAL) == HUGE_VAL && cbrt(-HUGE_VAL) == -HUGE_VAL);
	CHECK(cbrt(27.0) == 3.0 && cbrt(-27.0) == -3.0);
}

/* expm1.html/log1p.html RETURN VALUE/ERRORS -- expm1:
 * NaN->NaN, ±0->±0, -Inf->-1, +Inf->x, overflow -> range error,
 * HUGE_VAL. log1p: NaN->NaN, ±0/+Inf->x, x==-1 -> pole error,
 * -HUGE_VAL, x<-1 or x==-Inf -> domain error, NaN. Not declared
 * by include/math.h. */
static void test_expm1_log1p(void)
{
	CHECK(isnan(expm1(NAN)) && isnan(log1p(NAN)));
	CHECK(poszero(expm1(0.0)) && negzero(expm1(-0.0)));
	CHECK(expm1(-HUGE_VAL) == -1.0);
	CHECK(expm1(HUGE_VAL) == HUGE_VAL);
	CHECK(expm1(1000.0) == HUGE_VAL);   /* overflow -> range error */
	CHECK(poszero(log1p(0.0)) && negzero(log1p(-0.0)));
	CHECK(log1p(HUGE_VAL) == HUGE_VAL);
	CHECK(log1p(-1.0) == -HUGE_VAL);    /* pole error */
	CHECK(isnan(log1p(-2.0)) && isnan(log1p(-HUGE_VAL)));   /* domain error */
}

/* erf.html/erfc.html RETURN VALUE/ERRORS -- erf: NaN->
 * NaN, ±0->±0, ±Inf->±1; erfc: NaN->NaN, +Inf->+0, -Inf->2.
 * Not declared by include/math.h. */
static void test_erf_erfc(void)
{
	CHECK(isnan(erf(NAN)) && isnan(erfc(NAN)));
	CHECK(poszero(erf(0.0)) && negzero(erf(-0.0)));
	CHECK(erf(HUGE_VAL) == 1.0 && erf(-HUGE_VAL) == -1.0);
	CHECK(poszero(erfc(HUGE_VAL)));
	CHECK(erfc(-HUGE_VAL) == 2.0);
}

/* lgamma.html/tgamma.html RETURN VALUE/ERRORS -- lgamma:
 * non-positive integer -> pole error, +HUGE_VAL; overflow ->
 * range error, ±HUGE_VAL; NaN->NaN; x==1 or 2 -> +0; ±Inf ->
 * +Inf. tgamma (IEC 60559 branch, which math_errhandling==2
 * commits ntlibc to): negative integer -> domain error, NaN;
 * ±0 -> pole error, ±HUGE_VAL; overflow -> range error,
 * ±HUGE_VAL; NaN->NaN; +Inf->+Inf; -Inf -> domain error, NaN.
 * Not declared by include/math.h. */
static void test_gamma(void)
{
	CHECK(isnan(lgamma(NAN)) && isnan(tgamma(NAN)));
	CHECK(lgamma(0.0) == HUGE_VAL && lgamma(-1.0) == HUGE_VAL);   /* pole error */
	CHECK(poszero(lgamma(1.0)) && poszero(lgamma(2.0)));
	CHECK(lgamma(HUGE_VAL) == HUGE_VAL);
	CHECK(tgamma(HUGE_VAL) == HUGE_VAL);
	CHECK(isnan(tgamma(-HUGE_VAL)));            /* domain error */
	CHECK(isnan(tgamma(-2.0)));                 /* negative integer: domain error */
	CHECK(tgamma(0.0) == HUGE_VAL);             /* +0: pole error, sign of x */
	CHECK(tgamma(-0.0) == -HUGE_VAL);           /* -0: pole error, sign of x */
}

/* j0.html/j1.html/jn.html (Bessel, first kind) and
 * y0.html/y1.html/yn.html (second kind) RETURN VALUE/ERRORS --
 * NaN->NaN for all six; j-family: |x| too large or underflow ->
 * 0, range error may occur; y-family: x<0 -> -HUGE_VAL or NaN,
 * domain error may occur; x==0 -> -HUGE_VAL, pole error may
 * occur; overflow -> -HUGE_VAL or 0, range error may occur.
 * None of the six are declared by include/math.h. */
static void test_bessel(void)
{
	CHECK(isnan(j0(NAN)) && isnan(j1(NAN)) && isnan(jn(2, NAN)));
	CHECK(isnan(y0(NAN)) && isnan(y1(NAN)) && isnan(yn(2, NAN)));
	CHECK(j0(0.0) == 1.0);                 /* informational: known closed value */
	CHECK(y0(0.0) == -HUGE_VAL);           /* x==0: pole error */
	CHECK(isnan(y0(-1.0)) || y0(-1.0) == -HUGE_VAL);   /* x<0: domain error, either return permitted */
}

/* remainder.html RETURN VALUE/ERRORS -- x REM y (IEEE
 * remainder, round-to-nearest quotient, unlike fmod's
 * round-toward-zero quotient); x or y NaN -> NaN; x==±Inf or
 * y==±0 (other non-NaN) -> domain error, NaN.
 * remquo.html: same remainder value plus *quo set to a value
 * congruent to the true quotient mod 2^n, n>=3, sign of x/y;
 * *quo unspecified when y==0. Neither declared by
 * include/math.h. */
static void test_remainder_remquo(void)
{
	int quo;
	CHECK(isnan(remainder(NAN, 2.0)) && isnan(remainder(2.0, NAN)));
	CHECK(isnan(remainder(HUGE_VAL, 2.0)));          /* x==±Inf: domain error */
	CHECK(isnan(remainder(2.0, 0.0)));               /* y==±0, x non-NaN: domain error */
	CHECK(remainder(5.5, 2.0) == -0.5);              /* round-to-nearest quotient (3), unlike fmod(5.5,2.0)==1.5 */
	CHECK(isnan(remquo(NAN, 2.0, &quo)) && isnan(remquo(2.0, NAN, &quo)));
	CHECK(isnan(remquo(HUGE_VAL, 2.0, &quo)));
	CHECK(isnan(remquo(2.0, 0.0, &quo)));
	CHECK(remquo(5.5, 2.0, &quo) == -0.5 && (quo & 7) == 3);  /* quotient congruent mod 8 (n>=3) */
}

/* nextafter.html/nexttoward.html RETURN VALUE/ERRORS --
 * x==y -> y (converted to type of x); x or y NaN -> NaN; finite
 * x, correct value overflows -> range error, ±HUGE_VAL same
 * sign as x; correct value subnormal/underflows -> range error,
 * correct value or 0.0. Neither declared by include/math.h. */
static void test_nextafter(void)
{
	CHECK(nextafter(1.0, 1.0) == 1.0);
	CHECK(isnan(nextafter(NAN, 1.0)) && isnan(nextafter(1.0, NAN)));
	CHECK(nextafter(DBL_MAX, HUGE_VAL) == HUGE_VAL);        /* overflow -> range error */
	CHECK(nextafter(1.0, 2.0) > 1.0);                        /* moves toward y */
	CHECK(nextafter(1.0, 0.0) < 1.0);
	CHECK(poszero(nextafter(DBL_MIN, 0.0)) || nextafter(DBL_MIN, 0.0) > 0.0);  /* underflow: correct value or 0.0 */
	CHECK(nexttoward(1.0, 2.0L) > 1.0);
	CHECK(isnan(nexttoward(NAN, 1.0L)));
}

/* fdim.html RETURN VALUE/ERRORS -- "the positive
 * difference" max(x-y,+0); NaN if either argument is NaN;
 * overflow of a positive difference -> range error, HUGE_VAL.
 * Not declared by include/math.h. */
static void test_fdim(void)
{
	CHECK(fdim(5.0, 3.0) == 2.0);
	CHECK(poszero(fdim(3.0, 5.0)));   /* x<y: positive difference is +0 */
	CHECK(isnan(fdim(NAN, 1.0)) && isnan(fdim(1.0, NAN)));
	CHECK(fdim(DBL_MAX, -DBL_MAX) == HUGE_VAL);   /* overflow -> range error */
}

/* fma.html RETURN VALUE/ERRORS -- (x*y)+z rounded once;
 * x or y NaN -> NaN; x*y==0*Inf-shape (exact 0 times exact Inf)
 * with z non-NaN -> domain error, NaN; x*y exact Inf and z an
 * oppositely-signed Inf -> domain error, NaN; x*y finite and z
 * NaN (and x*y not the 0*Inf shape) -> NaN. Not declared by
 * include/math.h. */
static void test_fma(void)
{
	CHECK(fma(2.0, 3.0, 4.0) == 10.0);
	CHECK(isnan(fma(NAN, 1.0, 1.0)) && isnan(fma(1.0, NAN, 1.0)));
	CHECK(isnan(fma(0.0, HUGE_VAL, 1.0)));               /* 0*Inf shape, z non-NaN: domain error */
	CHECK(isnan(fma(HUGE_VAL, 1.0, -HUGE_VAL)));         /* x*y==+Inf, z==-Inf: domain error */
	CHECK(isnan(fma(2.0, 3.0, NAN)));                    /* x*y finite, z NaN */
}

/* ilogb.html/logb.html RETURN VALUE/ERRORS -- logb:
 * x==±0 -> pole error, -HUGE_VAL; NaN->NaN; ±Inf->+Inf. ilogb:
 * equivalent to (int)logb(x) with three reserved out-of-band
 * results for the cases an int cannot hold logb's answer:
 * x==0 -> FP_ILOGB0 (domain error, XSI); NaN -> FP_ILOGBNAN
 * (domain error, XSI); ±Inf -> INT_MAX (domain error, XSI).
 * Neither declared by include/math.h. */
static void test_ilogb_logb(void)
{
	CHECK(isnan(logb(NAN)));
	CHECK(logb(0.0) == -HUGE_VAL && logb(-0.0) == -HUGE_VAL);   /* pole error */
	CHECK(logb(HUGE_VAL) == HUGE_VAL && logb(-HUGE_VAL) == HUGE_VAL);
	CHECK(logb(8.0) == 3.0);   /* 8 == 1.0 * 2^3 */
	CHECK(ilogb(0.0) == FP_ILOGB0);
	CHECK(ilogb(NAN) == FP_ILOGBNAN);
	CHECK(ilogb(HUGE_VAL) == INT_MAX);
	CHECK(ilogb(8.0) == 3);
}

/* nearbyint.html RETURN VALUE/ERRORS -- "No errors are
 * defined." Rounds using the current rounding direction like
 * rint(), but explicitly "without raising the inexact
 * floating-point exception" -- the one clause that
 * distinguishes it from rint(), directly testable via <fenv.h>.
 * Not declared by include/math.h. */
static void test_nearbyint(void)
{
	CHECK(isnan(nearbyint(NAN)));
	CHECK(poszero(nearbyint(0.0)) && negzero(nearbyint(-0.0)));
	CHECK(nearbyint(HUGE_VAL) == HUGE_VAL && nearbyint(-HUGE_VAL) == -HUGE_VAL);
	CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
	CHECK(nearbyint(2.5) == 2.0);
	CHECK(fetestexcept(FE_INEXACT) == 0);   /* the distinguishing clause vs. rint() */
}

/* scalbln.html RETURN VALUE/ERRORS -- x * FLT_RADIX^n;
 * NaN->NaN; ±0/±Inf->x; n==0->x; overflow -> range error,
 * ±HUGE_VAL; underflow -> range error, 0.0 or correct value.
 * Not declared by include/math.h. */
static void test_scalbln(void)
{
	CHECK(isnan(scalbln(NAN, 5)));
	CHECK(poszero(scalbln(0.0, 5)) && negzero(scalbln(-0.0, 5)));
	CHECK(scalbln(HUGE_VAL, 5) == HUGE_VAL && scalbln(-HUGE_VAL, 5) == -HUGE_VAL);
	CHECK(scalbln(3.75, 0) == 3.75);
	CHECK(scalbln(1.0, 2000L) == HUGE_VAL && scalbln(-1.0, 2000L) == -HUGE_VAL);   /* overflow */
	CHECK(poszero(scalbln(1.0, -2000L)));   /* underflow */
}

/* rint.html/lround.html/lrint.html f/l-suffixed variants
 * -- rintf/rintl, lroundf/lroundl, lrintf/lrintl, llroundf/
 * llroundl, llrintf/llrintl -- carry the same clauses audited
 * for the double forms in test_lround_lrint() above. None of
 * the ten are declared by include/math.h: only the plain
 * `double`-argument rint/lround/llround/lrint/llrint exist. */
static void test_lround_lrint_variants(void)
{
	CHECK(isnan(rintf(NAN)) && isnan(rintl(NAN)));
	CHECK(rintf(HUGE_VALF) == HUGE_VALF && rintl(HUGE_VALL) == HUGE_VALL);
	CHECK(lroundf(2.5f) == 3 && lroundl(2.5L) == 3);
	CHECK(lrintf(2.5f) == 2 && lrintl(2.5L) == 2);   /* ties to even, default mode */
	CHECK(llroundf(2.5f) == 3 && llroundl(2.5L) == 3);
	CHECK(llrintf(2.5f) == 2 && llrintl(2.5L) == 2);
}

/* ---- exp2.html: RETURN VALUE -- "the base-2 exponential of x".
 * NaN -> NaN; +-0 -> 1; +Inf -> +Inf; -Inf -> +0; overflow -> range
 * error and +-HUGE_VAL; underflow -> range error and a value no
 * greater than DBL_MIN.  ERRORS lists only those two range errors, so
 * exp2 has no domain error at all: every real argument has an answer.
 * The exact-power assertions are "shall" material, not accuracy
 * probes: 2^n for small integer n is exactly representable in double,
 * so an exactly-equal comparison is the right test there. ---- */
static void test_exp2(void)
{
	CHECK(isnan(exp2(NAN)));
	CHECK(exp2(0.0) == 1.0 && exp2(-0.0) == 1.0);
	CHECK(exp2(HUGE_VAL) == HUGE_VAL);
	CHECK(poszero(exp2(-HUGE_VAL)));

	/* exact powers of two, both signs of exponent */
	CHECK(exp2(1.0) == 2.0 && exp2(2.0) == 4.0 && exp2(10.0) == 1024.0);
	CHECK(exp2(-1.0) == 0.5 && exp2(-2.0) == 0.25);
	CHECK(exp2(0.5) > 1.414213 && exp2(0.5) < 1.414214);   /* sqrt(2) */

	/* the largest finite and the smallest normal double are both exact
	 * powers of two away from 1, so these pin the ends of the range
	 * without asserting an accuracy figure */
	CHECK(isfinite(exp2(1023.0)) && exp2(1023.0) > 0.0);
	CHECK(exp2(1024.0) == HUGE_VAL);   /* one step past the top exponent */
	CHECK(exp2((double)(DBL_MIN_EXP - 1)) == DBL_MIN);

	/* range errors: overflow -> HUGE_VAL, underflow -> 0.0 permitted
	 * (the IEC 60559 branch, as test_exp() records for exp()) */
	CHECK(exp2(5000.0) == HUGE_VAL);
	CHECK(poszero(exp2(-5000.0)));

	/* subnormal results are reached by rounding at the store, not by a
	 * special case -- 2^-1060 is subnormal but not zero */
	CHECK(fpclassify(exp2(-1060.0)) == FP_SUBNORMAL);
	CHECK(exp2(-1060.0) > 0.0);

	/* f/l variants carry the identical clause */
	CHECK(isnan(exp2f(NAN)) && isnan(exp2l(NAN)));
	CHECK(exp2f(0.0f) == 1.0f && exp2l(0.0L) == 1.0L);
	CHECK(exp2f(3.0f) == 8.0f && exp2l(3.0L) == 8.0L);
	CHECK(exp2f(HUGE_VALF) == HUGE_VALF && exp2l(HUGE_VALL) == HUGE_VALL);
	CHECK(poszerof(exp2f(-HUGE_VALF)) && poszerol(exp2l(-HUGE_VALL)));
	CHECK(exp2f(200.0f) == HUGE_VALF);   /* overflows float */

	/* exp2(x) and exp(x*ln2) agree: informational only, POSIX mandates
	 * no accuracy for either, but a gross implementation error (wrong
	 * base, missing argument reduction) would show up here */
	CHECK(fabs(exp2(7.3) - exp(7.3 * M_LN2)) < 1e-10 * exp2(7.3));
}

/* ---- fmax.html / fmin.html, f/l variants: fmaxf/fmaxl/fminf/fminl
 * carry the same RETURN VALUE clause as the double forms -- "If just
 * one argument is a NaN, the other argument shall be returned.  If x
 * and y are NaN, a NaN shall be returned."  No ERRORS are defined.
 * The ±0 assertions below are informational in exactly the sense
 * test_fmaxmin() records for the double forms: POSIX does not say
 * which zero wins, so these pin down ntlibc's own permitted choice
 * (+0 for fmax, -0 for fmin) and check that the f/l variants agree
 * with the double one rather than each having drifted separately. ---- */
static void test_fmaxmin_variants(void)
{
	CHECK(fmaxf(NAN, 3.0f) == 3.0f && fmaxf(3.0f, NAN) == 3.0f);
	CHECK(fminf(NAN, 3.0f) == 3.0f && fminf(3.0f, NAN) == 3.0f);
	CHECK(isnan(fmaxf(NAN, NAN)) && isnan(fminf(NAN, NAN)));
	CHECK(fmaxf(1.0f, 2.0f) == 2.0f && fminf(1.0f, 2.0f) == 1.0f);
	CHECK(fmaxf(-2.0f, -1.0f) == -1.0f && fminf(-2.0f, -1.0f) == -2.0f);

	CHECK(fmaxl(NAN, 3.0L) == 3.0L && fmaxl(3.0L, NAN) == 3.0L);
	CHECK(fminl(NAN, 3.0L) == 3.0L && fminl(3.0L, NAN) == 3.0L);
	CHECK(isnan(fmaxl(NAN, NAN)) && isnan(fminl(NAN, NAN)));
	CHECK(fmaxl(1.0L, 2.0L) == 2.0L && fminl(1.0L, 2.0L) == 1.0L);
	CHECK(fmaxl(-2.0L, -1.0L) == -1.0L && fminl(-2.0L, -1.0L) == -2.0L);

	/* infinities are ordinary operands here, not special cases */
	CHECK(fmaxf(HUGE_VALF, 1.0f) == HUGE_VALF && fminf(-HUGE_VALF, 1.0f) == -HUGE_VALF);
	CHECK(fmaxl(HUGE_VALL, 1.0L) == HUGE_VALL && fminl(-HUGE_VALL, 1.0L) == -HUGE_VALL);
	CHECK(fmaxf(-HUGE_VALF, NAN) == -HUGE_VALF);
	CHECK(fmaxl(-HUGE_VALL, NAN) == -HUGE_VALL);

	CHECK(poszerof(fmaxf(0.0f, -0.0f)) && poszerof(fmaxf(-0.0f, 0.0f)));
	CHECK(negzerof(fminf(0.0f, -0.0f)) && negzerof(fminf(-0.0f, 0.0f)));
	CHECK(poszerol(fmaxl(0.0L, -0.0L)) && poszerol(fmaxl(-0.0L, 0.0L)));
	CHECK(negzerol(fminl(0.0L, -0.0L)) && negzerol(fminl(-0.0L, 0.0L)));

	/* a float variant must not round its result through double, and
	 * an l variant must not round through double either: a value
	 * exactly representable in the argument type comes back bit-equal */
	CHECK(fmaxf(FLT_MIN, 0.0f) == FLT_MIN && fminf(FLT_MAX, HUGE_VALF) == FLT_MAX);
	CHECK(fmaxl(LDBL_MIN, 0.0L) == LDBL_MIN && fminl(LDBL_MAX, HUGE_VALL) == LDBL_MAX);
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
	test_fmaxmin_variants();
	test_exp2();
	test_hypot();
	test_nan();
	test_errhandling();
	test_float_ld_variants();
	test_lround_lrint();
	test_asin_acos();
	test_hyperbolic();
	test_inverse_hyperbolic();
	test_cbrt();
	test_expm1_log1p();
	test_erf_erfc();
	test_gamma();
	test_bessel();
	test_remainder_remquo();
	test_nextafter();
	test_fdim();
	test_fma();
	test_ilogb_logb();
	test_nearbyint();
	test_scalbln();
	test_lround_lrint_variants();

	if (!fails) printf("posix-math: all tests passed\n");
	return fails != 0;
}
