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
 * in src/math/x87.h (fsqrt, fsin, fyl2x, ...), regardless of arch:
 * x87.h's helpers are inline x87 asm on both i386 and
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
 * units are live -- x87.h's helpers set x87 flags, ordinary compiled
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
 */
#ifndef _FENV_H
#define _FENV_H

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

/* The x87 FSTENV/FLDENV 28-byte image (control word, status word, tag
 * word, last instruction pointer/opcode, last data pointer -- only
 * the control and status words are meaningful to this header; the
 * rest round-trips opaquely), plus MXCSR on x86_64 where it is a
 * second, independent piece of live state. */
typedef struct {
	unsigned char __x87env[28];
#ifndef __i386__
	unsigned int __mxcsr;
#endif
} fenv_t;

#define FE_DFL_ENV ((const fenv_t *)-1)

int feclearexcept(int);
int fegetexceptflag(fexcept_t *, int);
int feraiseexcept(int);
int fesetexceptflag(const fexcept_t *, int);
int fetestexcept(int);

int fegetround(void);
int fesetround(int);

int fegetenv(fenv_t *);
int feholdexcept(fenv_t *);
int fesetenv(const fenv_t *);
int feupdateenv(const fenv_t *);

#ifdef __cplusplus
}
#endif

#endif
