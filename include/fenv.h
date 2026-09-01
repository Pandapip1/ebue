/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <fenv.h>: math.h's `math_errhandling` is unconditionally
 * MATH_ERREXCEPT (see include/math.h), which per basedefs/math.h.html
 * requires this header to exist and to make FE_DIVBYZERO, FE_INVALID,
 * FE_OVERFLOW (and, for a genuinely usable contract, FE_UNDERFLOW and
 * FE_INEXACT too) real, observable exception flags -- not just macros.
 *
 * Both build arches route through real x87 hardware for every helper
 * in src/math/ldbl_math.h (fsqrt, fsin, fyl2x, ...), regardless of arch:
 * ldbl_math.h's helpers are inline x87 asm on both i386 and
 * x86_64. But arithmetic the *compiler itself* emits for a plain
 * `double` expression (`a*b`, `a/b`, comparisons, ...) differs by
 * arch and by compiler:
 *
 *  - tcc/i386 (this project's primary compiler on that arch) emits
 *    x87 instructions for `double` arithmetic -- confirmed by
 *    compiling a trivial `double f(double,double)` and inspecting the
 *    object code (fldl/fmull/fsubl/fdivrl, no SSE at all).
 *
 *  - tcc/x86_64 emits SSE2 (mulsd/subsd/divsd/...) for the same
 *    source, per the Win64 ABI's mandate that double arguments travel
 *    in XMM registers -- confirmed the same way.
 *
 * So on i386, the x87 status word alone is authoritative: nothing in
 * this codebase's compiler output ever touches SSE. On x86_64, both
 * units are live -- ldbl_math.h's helpers set x87 flags, ordinary compiled
 * arithmetic sets MXCSR flags -- and a correct implementation must
 * observe and clear *both*; every function below does exactly that
 * (compile-time `#ifdef __i386__`, not a runtime CPU-feature probe,
 * since tcc/i386 never emits SSE regardless of the host CPU's actual
 * capabilities).
 *
 * mingw-w64 gcc (the configure-time fallback, see ./configure) always
 * targets SSE2 for x86_64 the same way, and for i386 tracks whatever
 * -mfpmath the build uses; ntlibc's own configure does not force
 * -mfpmath=sse there, so gcc/i386 also stays on x87 by default,
 * matching tcc/i386's assumption above.
 *
 * fenv_t's layout (x87 FSTENV/FLDENV image, plus MXCSR on x86_64) and
 * the FE_* exception bit values (the literal x87/SSE status-word bit
 * positions: bit 0 invalid, 2 divide-by-zero, 3 overflow, 4
 * underflow, 5 inexact -- bit 1, denormal, is not part of the C99 set
 * and is left unnamed) follow the same well-proven encoding musl uses
 * for these two arches (src/fenv/{i386,x86_64}/fenv.s upstream).
 *
 * The FE_* NUMBERS themselves stay the same fixed, portable values on
 * every arch this library builds for, including ones (aarch64) whose
 * real hardware status register numbers them completely differently
 * (FPSR: bit 0 invalid, 1 divide-by-zero, 2 overflow, 3 underflow, 4
 * inexact -- no gap at bit 1) -- src/math/fenv.c's own aarch64 half
 * translates between the two internally, the same way it is the one
 * place that knows x87 bit 1 (denormal) has no FE_* name at all. Only
 * fenv_t's actual STORAGE layout is genuinely arch-specific (this
 * header's one real ABI difference across arches), which is why only
 * that piece moved out to bits/fenv.h -- the same split arch/$(ARCH)/
 * bits/setjmp.h already has from include/setjmp.h, for the same
 * reason (jmp_buf's real layout is a hard arch fact; the FE_-macro
 * and setjmp() call surface is not).
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

/* fegetexceptflag/fesetexceptflag's flagp is a required output/input
 * parameter (ISO C 7.6.4.3/7.6.4.4) with no "skip it" convention, and
 * src/math/fenv.c writes/reads through it on every path, unconditionally
 * -- unlike fesetenv/fegetenv below, there is no FE_DFL_ENV-style
 * sentinel value here that a nonnull attribute could conflict with. */
int feclearexcept(int);
int fegetexceptflag(fexcept_t *, int) __attribute__((nonnull(1)));
int feraiseexcept(int);
int fesetexceptflag(const fexcept_t *, int) __attribute__((nonnull(1)));
int fetestexcept(int);

int fegetround(void);
int fesetround(int);

/* fegetenv/fesetenv's envp is likewise required, and src/math/fenv.c
 * dereferences it directly with no NULL check on either -- fesetenv only
 * special-cases FE_DFL_ENV (`(const fenv_t *)-1`, defined above), which
 * already satisfies `nonnull` (the attribute only excludes the literal
 * null pointer, not -1), so marking it does not conflict with that
 * sentinel. feholdexcept/feupdateenv are left unmarked: both simply
 * forward envp into fegetenv/fesetenv without dereferencing it
 * themselves, so there is nothing in THEIR own bodies for a nonnull
 * attribute to describe. */
int fegetenv(fenv_t *) __attribute__((nonnull(1)));
int feholdexcept(fenv_t *);
int fesetenv(const fenv_t *) __attribute__((nonnull(1)));
int feupdateenv(const fenv_t *);

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
