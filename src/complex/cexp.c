/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* cexp/clog/cpow/csqrt -- POSIX .../functions/{cexp,clog,cpow,
 * csqrt}.html, each deferring to C99 Annex G.6.3.1/G.6.2.2/G.6.4.1/
 * G.5.3.1. All four are expressible in exp/log/hypot/atan2, which this
 * library already has (src/math/exp.c, log.c, hypot.c, trig.c) --
 * test/posix-complex.c's own banner names this the "expressible in
 * exp/log/hypot/atan2" group.
 *
 * cexp's overflow-avoidance scaling (the `__cplx_scaled_exp` branches
 * below) is adapted from FreeBSD lib/msun/src/{s_cexp.c,k_exp.c} (David
 * Schultz, 2011; also the origin of musl's src/complex/cexp.c and
 * __cexp.c, MIT-relicensed unchanged, fetched from
 * https://github.com/kraj/musl per this tree's established practice --
 * see src/math/aarch64_math.h's own banner) for which special cases
 * exist and in what order; the scaling ARITHMETIC itself is this
 * file's own, built on this library's own exp()/scalbn() rather than
 * FreeBSD's hi/lo-word bit-splitting -- see complex_impl.h's own
 * __cplx_scaled_exp banner for why that substitution is exact, not an
 * approximation of the original technique.
 *
 * The overflow/underflow threshold constants below (709.78, 1454.3) are
 * deliberately rounded to a few significant digits rather than
 * transcribed bit-for-bit from FreeBSD's hex constants: they only
 * decide WHICH of two code paths a given x takes, both of which compute
 * the same value correctly (the plain path just isn't allowed to
 * overflow before reaching the final multiply) -- landing on the scaled
 * path a little earlier than the bit-exact boundary would costs nothing
 * but a slightly wider "always safe" margin, unlike a wrong exact value
 * or sign, which is why every OTHER constant in this file (THRESH in
 * csqrt below in particular) stays bit-exact instead. */
#include "complex_impl.h"

#ifndef __TINYC__

/* clog.html: "the complex natural (base e) logarithm of z, with a
 * branch cut along the negative real axis ... in the range of a strip
 * mathematically unbounded along the real axis and in the interval
 * [-i*pi, +i*pi] along the imaginary axis." log|z| + i*arg(z): cabs()
 * already places no upper bound on the real part, and carg() (this
 * directory's complex_parts.c) already places the branch cut and the
 * [-pi,+pi] range, signed zero included. */
double complex clog(double complex z)
{
	return CMPLX(log(cabs(z)), carg(z));
}
float complex clogf(float complex z) { return (float complex)clog((double complex)z); }
long double complex clogl(long double complex z) { return (long double complex)clog((double complex)z); }

/* cexp.html: "the complex exponent of z, defined as e**z." e**(x+iy) =
 * e**x * (cos y + i sin y); the special-value chain below is
 * FreeBSD/musl's own case order (see this file's own banner), the
 * scaling arithmetic is complex_impl.h's __cplx_scaled_exp. */
double complex cexp(double complex z)
{
	double x = creal(z), y = cimag(z);
	double exp_x, mag;

	/* cexp(x + I 0) = exp(x) + I 0 -- the returned zero keeps y's own
	 * sign, not a fixed +0, since this returns y itself. */
	if (y == 0.0)
		return CMPLX(exp(x), y);
	/* cexp(0 + I y) = cos(y) + I sin(y), y != 0 here. */
	if (x == 0.0)
		return CMPLX(cos(y), sin(y));

	if (!isfinite(y)) {
		/* x is finite-nonzero or NaN here (x == 0 already returned). */
		if (!isinf(x))
			return CMPLX(y - y, y - y);              /* NaN + NaN i */
		if (x < 0.0)
			return CMPLX(0.0, 0.0);                   /* -Inf +- I Inf|NaN */
		return CMPLX(x, y - y);                           /* +Inf +- I Inf|NaN */
	}

	if (x >= 709.78 && x <= 1454.3) {
		/* x between ln(DBL_MAX) and where exp(x) always overflows:
		 * scale to dodge a spurious overflow in a direct exp(x). */
		mag = __cplx_scaled_exp(x, 0);
		return CMPLX(cos(y) * mag, sin(y) * mag);
	}
	exp_x = exp(x);
	return CMPLX(exp_x * cos(y), exp_x * sin(y));
}
float complex cexpf(float complex z) { return (float complex)cexp((double complex)z); }
long double complex cexpl(long double complex z) { return (long double complex)cexp((double complex)z); }

/* cpow.html: "x**y, with a branch cut for the first parameter along the
 * negative real axis" -- C99 Annex G.6.4.1's own definition,
 * pow(x,y) = exp(y * log(x)). y*log(x) is complex*complex
 * multiplication, so it is done here on the extracted real components
 * by hand rather than through the `*` operator on two double complex
 * operands -- see complex_impl.h's own banner on why this directory
 * never lets the compiler synthesize __muldc3 for that. */
double complex cpow(double complex x, double complex y)
{
	double complex logx = clog(x);
	double lr = creal(logx), li = cimag(logx);
	double yr = creal(y), yi = cimag(y);
	double wr = yr * lr - yi * li;
	double wi = yr * li + yi * lr;

	return cexp(CMPLX(wr, wi));
}
float complex cpowf(float complex x, float complex y)
{
	return (float complex)cpow((double complex)x, (double complex)y);
}
long double complex cpowl(long double complex x, long double complex y)
{
	return (long double complex)cpow((double complex)x, (double complex)y);
}

/* csqrt.html: "the complex square root of z, with a branch cut along
 * the negative real axis ... in the range of the right half-plane
 * (including the imaginary axis)." Algorithm 312, CACM vol 10, Oct
 * 1967, via FreeBSD lib/msun/src/s_csqrt.c (David Schultz, 2007;
 * also musl's src/complex/csqrt.c origin, MIT-relicensed, fetched per
 * this file's own banner) -- the special-value chain and THRESH are
 * transcribed from there bit-for-bit; the final rescale is written as
 * explicit real-part arithmetic rather than `result *= 2` (a complex-
 * times-real-literal expression that Annex G.5.1p3 already permits a
 * compiler to fold without a runtime call, but complex_impl.h's own
 * discipline is to never depend on that fold being taken -- construct
 * every result componentwise instead). */
#define THRESH 0x1.a827999fcef32p+1022 /* DBL_MAX / (1 + sqrt(2)) */

double complex csqrt(double complex z)
{
	double a = creal(z), b = cimag(z);
	double t, re, im;
	int scale;

	if (a == 0.0 && b == 0.0)
		return CMPLX(0.0, b);
	if (isinf(b))
		return CMPLX(INFINITY, b);
	if (isnan(a)) {
		t = (b - b) / (b - b);      /* NaN; raises invalid if b is not NaN */
		return CMPLX(a, t);
	}
	if (isinf(a)) {
		/* csqrt(-inf + NaN i) = NaN +- inf i; csqrt(-inf + y i) = 0 + inf i
		 * csqrt(+inf + NaN i) = +inf + NaN i; csqrt(+inf + y i) = +inf + 0 i */
		if (signbit(a))
			return CMPLX(fabs(b - b), copysign(a, b));
		return CMPLX(a, copysign(b - b, b));
	}
	/* Remaining case (b is NaN, a finite) falls straight into the normal
	 * path below and comes out right there. */

	if (fabs(a) >= THRESH || fabs(b) >= THRESH) {
		a *= 0.25;
		b *= 0.25;
		scale = 1;
	} else {
		scale = 0;
	}

	if (a >= 0.0) {
		t = sqrt((a + hypot(a, b)) * 0.5);
		re = t;
		im = b / (2.0 * t);
	} else {
		t = sqrt((-a + hypot(a, b)) * 0.5);
		re = fabs(b) / (2.0 * t);
		im = copysign(t, b);
	}
	if (scale) {
		re *= 2.0;
		im *= 2.0;
	}
	return CMPLX(re, im);
}
float complex csqrtf(float complex z) { return (float complex)csqrt((double complex)z); }
long double complex csqrtl(long double complex z) { return (long double complex)csqrt((double complex)z); }

#endif /* !__TINYC__ */
