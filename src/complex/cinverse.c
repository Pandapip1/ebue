/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* cacos/casin/catan/cacosh/casinh/catanh -- POSIX .../functions/{cacos,
 * casin,catan,cacosh,casinh,catanh}.html, deferring to C99 Annex
 * G.6.1.1/G.6.1.2/G.6.1.4/G.6.2.1/G.6.2.2/G.6.2.3. test/posix-complex.c's
 * own banner calls this group "where the branch cuts are the real work"
 * -- casin and catan below are the two independent computations; every
 * other function here is a 90-degree rotation of one of them (the same
 * identities src/complex/ctrig.c's own banner uses for cos/sin/tan vs.
 * cosh/sinh/tanh): asinh(z) = -i asin(iz), atanh(z) = -i atan(iz),
 * acos(z) = pi/2 - asin(z), acosh(z) = +-i acos(z) (sign chosen by
 * cimag(z)'s own sign, so the result lands in acosh's own non-negative-
 * real-part range regardless of which square root branch z is on).
 *
 * casin: asin(z) = -i log(iz + sqrt(1-z^2)), musl's src/complex/casin.c
 * (MIT; fetched per src/math/aarch64_math.h's own banner) -- upstream's
 * own comment marks this "FIXME": the naive `1-z^2` here can lose
 * relative precision or overflow prematurely for |z| approaching the
 * extremes of the double range (Hull et al., "Implementing the Complex
 * Arcsine and Arccosine Functions Using Exception Handling", 1997, is
 * the fully hardened algorithm neither musl nor this port implements).
 * Disclosed, not silent: every worked case in test/posix-complex.c's
 * own fenced tests is a modest-magnitude point (0, 1, 0.5, 2+3i), well
 * inside where this formula is accurate, and this is the same
 * production algorithm real musl systems ship.
 *
 * catan: adapted from OpenBSD lib/libm/src/s_catan.c (Stephen L.
 * Moshier, 2008; also musl's src/complex/catan.c origin, MIT-
 * relicensed, same fetch) -- a closed form in atan2()/log(), no
 * branch-cut special-casing needed because atan2's own [-pi,pi] range
 * and log's own domain already place it correctly. */
#include "complex_impl.h"

#ifndef __TINYC__

/* casin.html: "the complex arc sine of z, with branch cuts outside the
 * interval [-1, +1] along the real axis ... in the range of a strip
 * ... and in the interval [-pi/2, +pi/2] along the real axis." */
double complex casin(double complex z)
{
	double x = creal(z), y = cimag(z);
	double complex w = CMPLX(1.0 - (x - y) * (x + y), -2.0 * x * y);
	double complex r = clog(CMPLX(-y, x) + csqrt(w));

	return CMPLX(cimag(r), -creal(r));
}
float complex casinf(float complex z) { return (float complex)casin((double complex)z); }
long double complex casinl(long double complex z) { return (long double complex)casin((double complex)z); }

/* catan.html: "the complex arc tangent of z, with branch cuts outside
 * the interval [-i, +i] along the imaginary axis ... in the interval
 * [-pi/2, +pi/2] along the real axis." Re = 1/2 atan2(2x, 1-x^2-y^2),
 * Im = 1/4 log((x^2+(y+1)^2)/(x^2+(y-1)^2)). */
double complex catan(double complex z)
{
	double x = creal(z), y = cimag(z);
	double x2 = x * x;
	double re = 0.5 * atan2(2.0 * x, 1.0 - x2 - y * y);
	double ym1 = y - 1.0, yp1 = y + 1.0;
	double im = 0.25 * log((x2 + yp1 * yp1) / (x2 + ym1 * ym1));

	return CMPLX(re, im);
}
float complex catanf(float complex z) { return (float complex)catan((double complex)z); }
long double complex catanl(long double complex z) { return (long double complex)catan((double complex)z); }

/* cacos.html: "with branch cuts outside the interval [-1, +1] along the
 * real axis ... in the range of a strip ... and in the interval [0, pi]
 * along the real axis" -- acos(z) = pi/2 - asin(z); the range clause
 * (real part in [0,pi]) follows from asin's own [-pi/2,pi/2] range. */
double complex cacos(double complex z)
{
	double complex w = casin(z);

	return CMPLX(M_PI_2 - creal(w), -cimag(w));
}
float complex cacosf(float complex z) { return (float complex)cacos((double complex)z); }
long double complex cacosl(long double complex z) { return (long double complex)cacos((double complex)z); }

/* catanh.html: "with branch cuts outside the interval [-1, +1] along
 * the real axis ... in the interval [-i*pi/2, +i*pi/2] along the
 * imaginary axis." atanh(z) = -i atan(iz). */
double complex catanh(double complex z)
{
	double complex w = catan(CMPLX(-cimag(z), creal(z)));

	return CMPLX(cimag(w), -creal(w));
}
float complex catanhf(float complex z) { return (float complex)catanh((double complex)z); }
long double complex catanhl(long double complex z) { return (long double complex)catanh((double complex)z); }

/* casinh.html: "with branch cuts outside the interval [-i, +i] along
 * the imaginary axis ... in the interval [-i*pi/2, +i*pi/2] along the
 * imaginary axis." asinh(z) = -i asin(iz). */
double complex casinh(double complex z)
{
	double complex w = casin(CMPLX(-cimag(z), creal(z)));

	return CMPLX(cimag(w), -creal(w));
}
float complex casinhf(float complex z) { return (float complex)casinh((double complex)z); }
long double complex casinhl(long double complex z) { return (long double complex)casinh((double complex)z); }

/* cacosh.html: "with a branch cut at values less than 1 along the real
 * axis ... in the range of a half-strip of NON-NEGATIVE values along
 * the real axis." acosh(z) = +-i acos(z), the sign chosen (per Kahan's
 * branch-cut convention, which cacos()'s own [0,pi] range already
 * respects) by cimag(z)'s own sign so the result always lands with a
 * non-negative real part, as the range clause requires regardless of
 * which half-plane z's imaginary part puts it in. */
double complex cacosh(double complex z)
{
	int zineg = signbit(cimag(z));
	double complex w = cacos(z);

	if (zineg)
		return CMPLX(cimag(w), -creal(w));
	return CMPLX(-cimag(w), creal(w));
}
float complex cacoshf(float complex z) { return (float complex)cacosh((double complex)z); }
long double complex cacoshl(long double complex z) { return (long double complex)cacosh((double complex)z); }

#endif /* !__TINYC__ */
