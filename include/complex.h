/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <complex.h> -- C99 complex arithmetic, Annex G (ISO/IEC 9899:1999).
 * POSIX.1-2017 defers every c*() function to this same ISO C text; see
 * test/posix-complex.c's banner for the full inventory and clause
 * citations.
 *
 * Under __TINYC__ this header defines only __STDC_NO_COMPLEX__ and
 * nothing else: tcc's parser rejects `_Complex` outright at parse time
 * on every tcc build this tree targets, which is a gap in the
 * compiler's own grammar (not a missing runtime symbol) that no
 * library code can work around. A caller that never names
 * `complex`/`_Complex` sees no difference from including this header;
 * one who does gets tcc's own parse error at their own use site.
 * Since the type never compiles under tcc, this header also never
 * needs __muldc3/__divdc3-style compiler-rt fallbacks for complex
 * multiply/divide there.
 *
 * Under every other compiler this tree builds with (clang, the native
 * Linux/aarch64 target; mingw-w64 gcc, the NT fallback), _Complex is a
 * real, hardware-ABI-backed language feature, and every function below
 * has a real algorithm in src/complex/ (adapted from musl, itself
 * largely adapted from FreeBSD/OpenBSD libm; see that directory's file
 * banners for the clause and reference each is adapted from).
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

/* __builtin_complex(re, im) constructs a complex value componentwise,
 * exactly -- unlike the expression `re + im*I`, which is complex
 * addition and multiplication and can, per Annex G's rules for
 * operands involving an infinity or a NaN, produce a different value
 * than the componentwise construction the CMPLX macros below promise.
 * _Complex_I is built the same way rather than as a literal `1.0i` so
 * construction goes through exactly one mechanism everywhere in this
 * header and in src/complex/. */
#define _Complex_I (__builtin_complex(0.0, 1.0))
#define I _Complex_I

/* CMPLX/CMPLXF/CMPLXL (C11 7.3.9.1, adopted here a standard early since
 * every compiler this branch targets already implements the builtin
 * they compile to): every branch-cut-sensitive function in
 * src/complex/ returns through one of these three, never through +, -,
 * or * on two already-complex operands -- see
 * src/complex/complex_impl.h's own banner for why that second style is
 * the one Annex G functions cannot safely use. */
#define __CMPLX(x, y, t) (__builtin_complex((t)(x), (t)(y)))
#define CMPLX(x, y)  __CMPLX(x, y, double)
#define CMPLXF(x, y) __CMPLX(x, y, float)
#define CMPLXL(x, y) __CMPLX(x, y, long double)

/* ---- creal/cimag/conj/cproj/carg/cabs (src/complex/complex_parts.c) --
 * decomposition, sign flips and one hypot()/atan2() call each; see that
 * file's own banner. */
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
