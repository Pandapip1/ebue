/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* creal/cimag/conj/cproj/carg/cabs -- POSIX .../functions/{creal,cimag,
 * conj,cproj,carg,cabs}.html, which each defer to C99 Annex G for the
 * exact value. This is the "essentially free" group test/posix-
 * complex.c's own banner names: extraction, one sign flip, and one
 * hypot()/atan2() call this library already has (src/math/hypot.c,
 * src/math/trig.c) -- nothing here is adapted from anywhere, each
 * function follows directly from its own one-sentence spec clause.
 *
 * Every `f`/`l` entry point below is a plain complex-precision CAST
 * around the one double body -- `(float complex)creal(...)` and
 * friends -- never an independent computation. C99 6.3.1.6 defines that
 * cast as an ordinary per-component real conversion, and for every
 * function in this file the round trip through double costs nothing:
 * creal/cimag/conj/cproj only ever select components, flip a sign, or
 * compare against infinity, none of which loses anything by being
 * evaluated one precision wider first (a value that started as float or
 * long double converts to double and back to its own precision
 * losslessly, since double can represent every float exactly and is
 * exactly what narrows back out of a long double `(double)` cast that
 * only ever sees values already in double's own range here). See
 * src/complex/complex_impl.h's own banner for why this cast, rather
 * than the compiler's synthesized __muldc3/__divdc3, is how every
 * function in this directory moves between precisions. */
#include "complex_impl.h"

#ifndef __TINYC__

double creal(double complex z) { return __real__ z; }
float crealf(float complex z) { return (float)creal((double complex)z); }
long double creall(long double complex z) { return (long double)creal((double complex)z); }

double cimag(double complex z) { return __imag__ z; }
float cimagf(float complex z) { return (float)cimag((double complex)z); }
long double cimagl(long double complex z) { return (long double)cimag((double complex)z); }

/* conj.html: "reversing the sign of its imaginary part" -- literally a
 * sign flip, stated as one, so that is what this does; no arithmetic
 * that could touch the sign of a zero any other way. */
double complex conj(double complex z) { return CMPLX(creal(z), -cimag(z)); }
float complex conjf(float complex z) { return (float complex)conj((double complex)z); }
long double complex conjl(long double complex z) { return (long double complex)conj((double complex)z); }

/* carg.html: "the argument ... with a branch cut along the negative
 * real axis ... in the interval [-pi, +pi]" -- exactly atan2's own
 * contract (src/math/trig.c's atan2, which already places the cut and
 * the sign of the result at a signed-zero imaginary part correctly; see
 * src/math/aarch64_math.h's __aa64_atan2 for where that sign is
 * decided). */
double carg(double complex z) { return atan2(cimag(z), creal(z)); }
float cargf(float complex z) { return (float)carg((double complex)z); }
long double cargl(long double complex z) { return (long double)carg((double complex)z); }

/* cabs.html: "the complex absolute value ... (also called norm,
 * modulus, or magnitude)" -- exactly hypot's own contract. */
double cabs(double complex z) { return hypot(creal(z), cimag(z)); }
float cabsf(float complex z) { return (float)cabs((double complex)z); }
long double cabsl(long double complex z) { return (long double)cabs((double complex)z); }

/* cproj.html: "a projection of z onto the Riemann sphere: z projects to
 * z, except that all complex infinities ... project to positive
 * infinity on the real axis. If z has an infinite part, then cproj(z)
 * shall be equivalent to INFINITY + I * copysign(0.0, cimag(z))" --
 * quoted, not paraphrased, because that sentence IS the algorithm: the
 * sign of the result's zero imaginary part comes from copysign on the
 * ORIGINAL z's imaginary part, not from any arithmetic on it. */
double complex cproj(double complex z)
{
	if (isinf(creal(z)) || isinf(cimag(z)))
		return CMPLX(INFINITY, copysign(0.0, cimag(z)));
	return z;
}
float complex cprojf(float complex z) { return (float complex)cproj((double complex)z); }
long double complex cprojl(long double complex z) { return (long double complex)cproj((double complex)z); }

#endif /* !__TINYC__ */
