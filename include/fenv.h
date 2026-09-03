/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <fenv.h>: math.h's `math_errhandling` is unconditionally MATH_ERREXCEPT,
 * which requires these to be real, observable exception flags, not just
 * macros.
 *
 * On i386 the x87 status word alone is authoritative: both tcc and the
 * mingw-w64 fallback emit x87 for plain `double` arithmetic there. On
 * x86_64 both x87 (src/math/ldbl_math.h's inline asm helpers) and SSE2/
 * MXCSR (ordinary compiled arithmetic, per the Win64 ABI) are live, so
 * every function below observes and clears both, decided at compile time
 * via `#ifdef __i386__` rather than a runtime CPU probe.
 *
 * The FE_* values are fixed, portable numbers on every arch this library
 * builds for, even though aarch64's real FPSR register numbers its bits
 * differently; src/math/fenv.c's aarch64 half translates between the two.
 * Only fenv_t's storage layout is genuinely arch-specific, which is why
 * that piece alone moved out to bits/fenv.h (the same split bits/setjmp.h
 * has from this header, for the same reason).
 */
#ifndef _FENV_H
#define _FENV_H

#include <bits/fenv.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FE_INVALID    0x01
#define FE_DIVBYZERO  0x04
#define FE_OVERFLOW   0x08
#define FE_UNDERFLOW  0x10
#define FE_INEXACT    0x20

#define FE_ALL_EXCEPT (FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW | FE_UNDERFLOW | FE_INEXACT)

/* x87 control-word / MXCSR rounding-control field values, already
 * shifted into the x87 CW's bit 10-11 position; fesetround() shifts
 * left by 3 more to reach MXCSR's bit 13-14 field on x86_64 -- same
 * 2-bit encoding, different offset. */
#define FE_TONEAREST  0x000
#define FE_DOWNWARD   0x400
#define FE_UPWARD     0x800
#define FE_TOWARDZERO 0xc00

typedef unsigned short fexcept_t;

/* __fenv_t comes from bits/fenv.h -- see that header for the real,
 * arch-specific layout (x87 FSTENV/FLDENV image (+ MXCSR on x86_64)
 * here; FPCR+FPSR on aarch64). */
typedef __fenv_t fenv_t;

#define FE_DFL_ENV ((const fenv_t *)-1)

int feclearexcept(int);
int fegetexceptflag(fexcept_t *, int) __attribute__((nonnull(1)));
int feraiseexcept(int);
int fesetexceptflag(const fexcept_t *, int) __attribute__((nonnull(1)));
int fetestexcept(int);

int fegetround(void);
int fesetround(int);

/* fesetenv's FE_DFL_ENV sentinel is (const fenv_t *)-1, not NULL, so it
 * doesn't conflict with nonnull here. */
int fegetenv(fenv_t *) __attribute__((nonnull(1)));
int feholdexcept(fenv_t *);
int fesetenv(const fenv_t *) __attribute__((nonnull(1)));
int feupdateenv(const fenv_t *);

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
