/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <complex.h> -- C99 complex arithmetic, Annex G (ISO/IEC 9899:1999).
 * POSIX.1-2017 .../functions/c*.html for every one of the 66 interfaces
 * below defers to this same ISO C text ("The functionality described on
 * this reference page is aligned with the ISO C standard"), so there is
 * one spec, not two -- see test/posix-complex.c's own banner for the
 * full 66-function inventory and the clause citations each fenced test
 * case below argues from.
 *
 * ==================== the tcc gap ======================================
 *
 * This header compiles to nothing but __STDC_NO_COMPLEX__ under
 * __TINYC__. That is not a scope choice this project made; it is a
 * probed, unconditional fact about every tcc build this tree targets --
 * x86_64-win32-tcc, arm64-win32-tcc, arm64-tcc, all the same "tcc version
 * 0.9.28rc 2026-08-23 HEAD@69eed4d3" build -- confirmed directly, the
 * same discipline src/math/aarch64_math.h's own __TINYC__ branch banner
 * describes for its inline-asm subset: a plain
 *
 *     double _Complex x;
 *
 * gets "_Complex is not yet supported" from every one of them, at parse
 * time, unconditionally. That is a hole in the compiler's own grammar,
 * not a missing runtime symbol -- src/internal/rtlib.h's territory,
 * where this tree routinely supplies the compiler-rt/libgcc functions a
 * -nostdlib build would otherwise lack (__extenddftf2 and friends, see
 * arch/aarch64/src/ld128_convert.c) -- and no amount of library code can
 * make a compiler parse a keyword it has not implemented. Concretely,
 * this also means no double/float/long double _Complex multiply or
 * divide operator ever needs to compile under tcc either, so this
 * header never has to supply __muldc3/__divdc3-style compiler-rt
 * fallbacks for it (see src/complex/complex_impl.h's own banner on how
 * the real implementation below avoids ever needing those on ANY
 * target, including the ones where _Complex does work).
 *
 * Rather than let that error surface at whatever macro-expansion site in
 * a caller's own translation unit happens to be first to write
 * `_Complex`, this header defines exactly the thing ISO C reserves for
 * an implementation in this position -- __STDC_NO_COMPLEX__ (C11
 * 7.3.1p2: defined "if and only if the implementation does not support
 * complex types") -- and contributes nothing else. A tcc caller who
 * never names `complex`/`_Complex` sees no difference from including
 * this header; one who does gets tcc's own honest parse error at their
 * own use site, which is the right layer for it to appear at.
 *
 * ==================== everywhere else ==================================
 *
 * Under every other compiler this tree builds with (clang, the native
 * Linux/aarch64 target; mingw-w64 gcc, the NT configure-time fallback),
 * C99 _Complex is a real, hardware-ABI-backed language feature, and
 * every function below has a real algorithm behind it in src/complex/ --
 * see that directory's own file banners for which C99 Annex G clause and
 * which reference implementation (musl's src/complex/ *.c, MIT licensed,
 * itself largely adapted from FreeBSD/OpenBSD libm -- fetched verbatim
 * from https://github.com/kraj/musl, not reconstructed from memory, this
 * tree's established practice for exactly this transcription-risk
 * reason; see src/math/aarch64_math.h's own banner) each is adapted
 * from.
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

/* __builtin_complex(re, im) is a real clang/gcc language builtin (not a
 * -fno-builtin library-call substitution -- that flag governs recognizing
 * calls to standard LIBRARY functions like memcpy as their compiler
 * intrinsic equivalent, an entirely different mechanism from a
 * dedicated __builtin_* language primitive like this one, confirmed by
 * compiling a throwaway use of it under this tree's exact
 * -std=c99 -nostdinc -fno-builtin flags). It constructs a complex value
 * from its real and imaginary parts EXACTLY, component by component --
 * never via the arithmetic expression `re + im*I`, which is complex
 * ADDITION and MULTIPLICATION and can therefore, per Annex G's own
 * rules for operands involving an infinity or a NaN, produce a
 * different value than the componentwise construction the CMPLX macros
 * below promise. This is also why _Complex_I is built the same way
 * rather than as a literal `1.0i` written directly: both spellings are
 * ordinary C99, but this keeps construction going through exactly one
 * mechanism everywhere in this header and in src/complex/. */
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
