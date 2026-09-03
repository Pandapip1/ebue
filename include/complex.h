/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <complex.h> -- C99 complex arithmetic, Annex G (ISO/IEC 9899:1999).
 *
 * tcc's parser rejects `_Complex` outright (a gap in its grammar, not a
 * missing runtime symbol), so under __TINYC__ this header defines only
 * __STDC_NO_COMPLEX__ and nothing else.
 */
#ifndef _COMPLEX_H
#define _COMPLEX_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __TINYC__

#define __STDC_NO_COMPLEX__ 1

#else /* !__TINYC__ */

#define complex _Complex

/* __builtin_complex(re, im) constructs componentwise; `re + im*I` is complex
 * multiplication and addition instead, which can differ per Annex G's rules
 * when an operand is infinite or NaN. */
#define _Complex_I (__builtin_complex(0.0, 1.0))
#define I _Complex_I

/* Branch-cut-sensitive functions in src/complex/ build results through one
 * of these rather than +, -, or * on two complex operands, for the same
 * infinity/NaN reason as _Complex_I above. */
#define __CMPLX(x, y, t) (__builtin_complex((t)(x), (t)(y)))
#define CMPLX(x, y)  __CMPLX(x, y, double)
#define CMPLXF(x, y) __CMPLX(x, y, float)
#define CMPLXL(x, y) __CMPLX(x, y, long double)

/* ---- creal/cimag/conj/cproj/carg/cabs (src/complex/complex_parts.c) ---- */
double      creal(double complex);
float       crealf(float complex);
long double creall(long double complex);
double      cimag(double complex);
float       cimagf(float complex);
long double cimagl(long double complex);
double complex      conj(double complex);
float complex        conjf(float complex);
long double complex  conjl(long double complex);
double      carg(double complex);
float       cargf(float complex);
long double cargl(long double complex);
double      cabs(double complex);
float       cabsf(float complex);
long double cabsl(long double complex);
double complex      cproj(double complex);
float complex        cprojf(float complex);
long double complex  cprojl(long double complex);

/* ---- cexp/clog/cpow/csqrt (src/complex/cexp.c) ----------------------- */
double complex      cexp(double complex);
float complex        cexpf(float complex);
long double complex  cexpl(long double complex);
double complex      clog(double complex);
float complex        clogf(float complex);
long double complex  clogl(long double complex);
double complex      cpow(double complex, double complex);
float complex        cpowf(float complex, float complex);
long double complex  cpowl(long double complex, long double complex);
double complex      csqrt(double complex);
float complex        csqrtf(float complex);
long double complex  csqrtl(long double complex);

/* ---- circular and hyperbolic trigonometry (src/complex/ctrig.c) ------ */
double complex      ccos(double complex);
float complex        ccosf(float complex);
long double complex  ccosl(long double complex);
double complex      csin(double complex);
float complex        csinf(float complex);
long double complex  csinl(long double complex);
double complex      ctan(double complex);
float complex        ctanf(float complex);
long double complex  ctanl(long double complex);
double complex      ccosh(double complex);
float complex        ccoshf(float complex);
long double complex  ccoshl(long double complex);
double complex      csinh(double complex);
float complex        csinhf(float complex);
long double complex  csinhl(long double complex);
double complex      ctanh(double complex);
float complex        ctanhf(float complex);
long double complex  ctanhl(long double complex);

/* ---- inverse trigonometry/hyperbolics, branch cuts (src/complex/
 * cinverse.c) --------------------------------------------------------- */
double complex      cacos(double complex);
float complex        cacosf(float complex);
long double complex  cacosl(long double complex);
double complex      casin(double complex);
float complex        casinf(float complex);
long double complex  casinl(long double complex);
double complex      catan(double complex);
float complex        catanf(float complex);
long double complex  catanl(long double complex);
double complex      cacosh(double complex);
float complex        cacoshf(float complex);
long double complex  cacoshl(long double complex);
double complex      casinh(double complex);
float complex        casinhf(float complex);
long double complex  casinhl(long double complex);
double complex      catanh(double complex);
float complex        catanhf(float complex);
long double complex  catanhl(long double complex);

#endif /* !__TINYC__ */

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
