/* C library internals intentionally use the implementation-reserved
 * namespace so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __divdc3 -- the compiler-generated call for `double complex a, b; a / b;`
 * (clang/gcc emit a call to this exact name for double complex division,
 * the Itanium C++ ABI / libgcc convention: confirmed empirically the
 * same way arch/aarch64/src/ld128_convert.c's own banner confirms its
 * three functions are needed -- `-S` on a throwaway complex division
 * under this tree's exact CFLAGS emits `bl __divdc3` and nothing else
 * for that operator). This build is -nostdlib and links no libgcc/
 * compiler-rt (Makefile), so without a definition here every double
 * complex division anywhere in this tree -- including
 * test/posix-complex.c's own fenced `csin(z) / ccos(z)` -- fails to
 * LINK, the same class of gap src/internal/rtlib.h's own banner
 * describes generally ("a wrong signature ... is not a compile error
 * but a miscompile at every call site the code generator emitted --
 * call sites that appear in no source file").
 *
 * Algorithm: ISO/IEC 9899:1999 (C99) Annex G.5.2's own sample
 * implementation of _Cdivd -- the "smart" division the standard
 * recommends specifically because the textbook (ac+bd)/(c^2+d^2),
 * (bc-ad)/(c^2+d^2) formula overflows/underflows its denominator far
 * sooner than the true quotient would (c^2+d^2 squares whichever of c,d
 * is larger, so it overflows for |c| or |d| as small as sqrt(DBL_MAX)
 * ~= 1.3e154, long before a well-conditioned quotient itself would).
 * G.5.2's fix: rescale the denominator's real and imaginary parts by
 * the power of two nearest their own common magnitude (via logb/scalbn,
 * both already in this library) before squaring, then undo the scale
 * on the way out -- and, since scaling can still leave the result NaN+
 * iNaN in the genuinely-infinite/zero edge cases logb/scalbn alone
 * cannot rescue, a second pass recovers exactly those per the standard's
 * own text. Transcribed from the standard's own informative sample
 * code, not reconstructed from memory, for the same transcription-risk
 * reason this tree cites throughout (src/math/aarch64_math.h's own
 * banner) -- variable names below match the standard's (a,b = numerator
 * real/imaginary; c,d = denominator real/imaginary; ilogbw, denom, x, y
 * as in the standard text) specifically so the two can be checked
 * against each other clause by clause.
 *
 * Only __divdc3 is supplied, not __muldc3/__divsc3/__mulsc3/__divtc3/
 * __multc3 -- see complex_impl.h's own banner for exactly which of
 * those this tree needs (none) and why the long-double pair especially
 * must stay unsupplied (they would require genuine quad-precision
 * soft-float arithmetic, ld128_convert.c's own explicitly-declared
 * scope boundary). */
#include "complex_impl.h"
#include "rtlib.h"   /* this function's own prototype -- src/internal/
                      * rtlib.h's own banner explains why a compiler-
                      * generated-call function like this needs one even
                      * though nothing in the tree calls it by name */

#ifndef __TINYC__

double complex __divdc3(double a, double b, double c, double d)
{
	double logbw, denom, x, y;
	int ilogbw = 0;

	logbw = logb(fmax(fabs(c), fabs(d)));
	if (isfinite(logbw)) {
		ilogbw = (int)logbw;
		c = scalbn(c, -ilogbw);
		d = scalbn(d, -ilogbw);
	}
	denom = c * c + d * d;
	x = scalbn((a * c + b * d) / denom, -ilogbw);
	y = scalbn((b * c - a * d) / denom, -ilogbw);

	/* Recover infinities and zeros that computed as NaN+iNaN despite the
	 * true quotient being finite and representable (C99 G.5.2's own
	 * three recovery cases, in the standard's own order). */
	if (isnan(x) && isnan(y)) {
		if (denom == 0.0 && (!isnan(a) || !isnan(b))) {
			x = copysign(INFINITY, c) * a;
			y = copysign(INFINITY, c) * b;
		} else if ((isinf(a) || isinf(b)) && isfinite(c) && isfinite(d)) {
			a = copysign(isinf(a) ? 1.0 : 0.0, a);
			b = copysign(isinf(b) ? 1.0 : 0.0, b);
			x = INFINITY * (a * c + b * d);
			y = INFINITY * (b * c - a * d);
		} else if (isinf(logbw) && logbw > 0.0 && isfinite(a) && isfinite(b)) {
			c = copysign(isinf(c) ? 1.0 : 0.0, c);
			d = copysign(isinf(d) ? 1.0 : 0.0, d);
			x = 0.0 * (a * c + b * d);
			y = 0.0 * (b * c - a * d);
		}
	}
	return CMPLX(x, y);
}

#endif /* !__TINYC__ */

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
