/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* ccosh/csinh/ctanh/ccos/csin/ctan -- POSIX .../functions/{ccosh,csinh,
 * ctanh,ccos,csin,ctan}.html, deferring to C99 Annex G.6.2.4/G.6.2.6/
 * G.6.2.7/G.6.5.4/G.6.5.6/G.6.5.7. test/posix-complex.c's own banner
 * groups these as one family because the identities cos(z)=cosh(iz),
 * sin(z)=-i*sinh(iz), tan(z)=-i*tanh(iz) make them one: ccosh/csinh/
 * ctanh below are the real work, ccos/csin/ctan are a 90-degree
 * rotation of the argument and, for sin/tan, of the result.
 *
 * ccosh/csinh: adapted from FreeBSD lib/msun/src/{s_ccosh.c,s_csinh.c}
 * (Bruce D. Evans and Steven G. Kargl, 2005) -- special-value case order
 * transcribed from there (also musl's src/complex/{ccosh,csinh}.c
 * origin, MIT-relicensed, fetched per src/math/aarch64_math.h's own
 * banner); the hi/lo IEEE-word bit tests those sources use are
 * translated here to isfinite()/isinf()/isnan()/copysign(), which this
 * library already provides via <math.h> -- a meaning-preserving
 * rewrite, not a re-derivation, checked branch by branch against the
 * original. The overflow-avoidance threshold constants (22.0, 709.78,
 * 1454.3) are rounded rather than bit-exact for the same reason
 * src/complex/cexp.c's own banner gives for its identical constants.
 *
 * ctanh: Kahan's algorithm (W. Kahan, "Branch Cuts for Complex
 * Elementary Functions", 1987), via FreeBSD lib/msun/src/s_ctanh.c
 * (David Schultz, 2011) / musl's src/complex/ctanh.c, same provenance.
 */
#include "complex_impl.h"

#ifndef __TINYC__

double complex ccosh(double complex z)
{
	double x = creal(z), y = cimag(z);
	double h, mag;

	if (isfinite(x) && isfinite(y)) {
		if (y == 0.0)
			return CMPLX(cosh(x), x * y);
		if (fabs(x) < 22.0)
			return CMPLX(cosh(x) * cos(y), sinh(x) * sin(y));
		if (fabs(x) < 709.78) {
			h = exp(fabs(x)) * 0.5;
			return CMPLX(h * cos(y), copysign(h, x) * sin(y));
		}
		if (fabs(x) < 1454.3) {
			mag = __cplx_scaled_exp(fabs(x), -1);
			return CMPLX(cos(y) * mag, sin(y) * mag * copysign(1.0, x));
		}
		/* |x| so large the result always overflows: 0x1p1023 * x
		 * deliberately overflows to +-inf carrying x's own sign. */
		h = 0x1p1023 * x;
		return CMPLX(h * h * cos(y), h * sin(y));
	}

	/* cosh(+-0 +- I Inf|NaN) = NaN + I (unspecified-sign) 0. */
	if (x == 0.0 && !isfinite(y))
		return CMPLX(y - y, copysign(0.0, x * (y - y)));

	/* cosh(+-Inf +- I 0) = +Inf + I (+-)(+-)0; cosh(NaN +- I 0) = NaN + I (unspecified)0. */
	if (y == 0.0 && !isfinite(x)) {
		if (isinf(x))
			return CMPLX(x * x, copysign(0.0, x) * y);
		return CMPLX(x * x, copysign(0.0, (x + x) * y));
	}

	/* cosh(x +- I Inf|NaN) = NaN + I NaN, finite nonzero x. */
	if (isfinite(x) && !isfinite(y))
		return CMPLX(y - y, x * (y - y));

	if (isinf(x)) {
		if (isinf(y))
			return CMPLX(x * x, x * (y - y));           /* cosh(+-Inf +- I Inf) */
		return CMPLX((x * x) * cos(y), x * sin(y));          /* cosh(+-Inf + I y) */
	}

	return CMPLX((x * x) * (y - y), (x + x) * (y - y));         /* NaN + I NaN */
}
float complex ccoshf(float complex z) { return (float complex)ccosh((double complex)z); }
long double complex ccoshl(long double complex z) { return (long double complex)ccosh((double complex)z); }

double complex csinh(double complex z)
{
	double x = creal(z), y = cimag(z);
	double h, mag;

	if (isfinite(x) && isfinite(y)) {
		if (y == 0.0)
			return CMPLX(sinh(x), y);
		if (fabs(x) < 22.0)
			return CMPLX(sinh(x) * cos(y), cosh(x) * sin(y));
		if (fabs(x) < 709.78) {
			h = exp(fabs(x)) * 0.5;
			return CMPLX(copysign(h, x) * cos(y), h * sin(y));
		}
		if (fabs(x) < 1454.3) {
			mag = __cplx_scaled_exp(fabs(x), -1);
			return CMPLX(cos(y) * mag * copysign(1.0, x), sin(y) * mag);
		}
		h = 0x1p1023 * x;
		return CMPLX(h * cos(y), h * h * sin(y));
	}

	if (x == 0.0 && !isfinite(y))
		return CMPLX(copysign(0.0, x * (y - y)), y - y);

	if (y == 0.0 && !isfinite(x)) {
		if (isinf(x))
			return CMPLX(x, y);
		return CMPLX(x, copysign(0.0, y));
	}

	if (isfinite(x) && !isfinite(y))
		return CMPLX(y - y, x * (y - y));

	if (isinf(x)) {
		if (isinf(y))
			return CMPLX(x * x, x * (y - y));
		return CMPLX(x * cos(y), INFINITY * sin(y));
	}

	return CMPLX((x * x) * (y - y), (x + x) * (y - y));
}
float complex csinhf(float complex z) { return (float complex)csinh((double complex)z); }
long double complex csinhl(long double complex z) { return (long double complex)csinh((double complex)z); }

double complex ctanh(double complex z)
{
	double x = creal(z), y = cimag(z);
	double t, beta, s, rho, denom;

	if (!isfinite(x)) {
		if (isnan(x))
			return CMPLX(x, (y == 0.0 ? y : x * y));
		x = copysign(1.0, x);      /* x is exactly +-Inf */
		return CMPLX(x, copysign(0.0, isinf(y) ? y : sin(y) * cos(y)));
	}

	if (!isfinite(y))
		return CMPLX(x != 0.0 ? y - y : x, y - y);

	if (fabs(x) >= 22.0) {
		/* tanh(+-huge + i y) ~= +-1 +- i 2 sin(2y)/exp(2x). */
		double exp_mx = exp(-fabs(x));
		return CMPLX(copysign(1.0, x), 4.0 * sin(y) * cos(y) * exp_mx * exp_mx);
	}

	t = tan(y);
	beta = 1.0 + t * t;        /* = 1 / cos^2(y) */
	s = sinh(x);
	rho = sqrt(1.0 + s * s);   /* = cosh(x) */
	denom = 1.0 + beta * s * s;
	return CMPLX((beta * rho * s) / denom, t / denom);
}
float complex ctanhf(float complex z) { return (float complex)ctanh((double complex)z); }
long double complex ctanhl(long double complex z) { return (long double complex)ctanh((double complex)z); }

/* cos(z) = cosh(iz); sin(z) = -i sinh(iz); tan(z) = -i tanh(iz) --
 * multiplying by i rotates a+bi to -b+ai, and by -i rotates back
 * (a+bi -> b-ai), both plain component swaps, not `*I` on a complex
 * operand. */
double complex ccos(double complex z)
{
	return ccosh(CMPLX(-cimag(z), creal(z)));
}
float complex ccosf(float complex z) { return (float complex)ccos((double complex)z); }
long double complex ccosl(long double complex z) { return (long double complex)ccos((double complex)z); }

double complex csin(double complex z)
{
	double complex w = csinh(CMPLX(-cimag(z), creal(z)));
	return CMPLX(cimag(w), -creal(w));
}
float complex csinf(float complex z) { return (float complex)csin((double complex)z); }
long double complex csinl(long double complex z) { return (long double complex)csin((double complex)z); }

double complex ctan(double complex z)
{
	double complex w = ctanh(CMPLX(-cimag(z), creal(z)));
	return CMPLX(cimag(w), -creal(w));
}
float complex ctanf(float complex z) { return (float complex)ctan((double complex)z); }
long double complex ctanl(long double complex z) { return (long double complex)ctan((double complex)z); }

#endif /* !__TINYC__ */
