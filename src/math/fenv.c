/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See include/fenv.h for why both the x87 status word and (on x86_64
 * only) MXCSR must be consulted: tcc/i386 compiles plain `double`
 * arithmetic to x87 instructions, tcc/x86_64 compiles it to SSE2, and
 * src/math/x87.h's inline-asm helpers (fsqrt, fsin, fyl2x, ...) are
 * x87 on both arches -- so on x86_64 either unit can be the one that
 * actually raised a given flag.
 *
 * The 28-byte x87 environment image used by fnstenv/fldenv below is
 * the standard 32-bit-protected-mode format (also what x86_64 gets
 * from a bare, non-REX.W fnstenv/fldenv): control word at byte
 * offset 0, status word at offset 4 (its low byte holding exactly the
 * six FE_* exception bits), tag word/instruction pointer/data pointer
 * beyond that, round-tripped opaquely by fegetenv/fesetenv without
 * this file caring what they mean.
 */
#include <fenv.h>

int feclearexcept(int excepts)
{
	/* Zero-initialised only to keep clang's static analyzer quiet: it
	 * cannot see that the fnstenv/fnstsw/fnstcw/stmxcsr below write
	 * through their pointer operand, so it treats the reads that
	 * follow as garbage values (clang-analyzer-core.Undefined
	 * BinaryOperatorResult; clang-tidy 18 reports it, 21 does not).
	 * The stores overwrite every byte, so these initialisers are dead
	 * in practice -- they are not a correctness fix. Passing the
	 * buffers as "=m" outputs instead would inform the analyzer
	 * properly, but tcc miscompiles that form here (SIGSEGV at
	 * runtime on both arches), so this is the portable option. */
	unsigned char env[28] = { 0 };
	excepts &= FE_ALL_EXCEPT;
	__asm__ __volatile__("fnstenv (%0)" : : "r"(env) : "memory");
	env[4] = (unsigned char)(env[4] & ~excepts);
	__asm__ __volatile__("fldenv (%0)" : : "r"(env) : "memory");
#ifndef __i386__
	{
		unsigned int mxcsr = 0;
		__asm__ __volatile__("stmxcsr (%0)" : : "r"(&mxcsr) : "memory");
		mxcsr &= ~(unsigned int)excepts;
		__asm__ __volatile__("ldmxcsr (%0)" : : "r"(&mxcsr) : "memory");
	}
#endif
	return 0;
}

/* This pokes the sticky exception bits directly (via fldenv/ldmxcsr)
 * rather than performing an operation that actually triggers each
 * exception in hardware. C99 7.6.2.3 permits that -- "the specified
 * exceptions" need only end up "currently set", not be raised through
 * a real computation. It is observationally identical to a real raise
 * as long as every exception stays masked, which they always are here
 * (src/internal never unmasks any, and neither does anything in this
 * file): a masked exception never traps, it only ever sets the sticky
 * flag, exactly what this does directly. If a caller did unmask an
 * exception (there is no API in this header to do so, but the bits
 * are real hardware state, reachable via fegetenv()'s raw env, or
 * from outside this library entirely) and then called this function
 * for that exception, the flag would still end up set correctly, but
 * no SIGFPE would fire the way it would for a genuine trapping
 * operation -- feraiseexcept()'s trap behaviour when unmasked is
 * merely "implementation-defined" per the same clause, and this is
 * the implementation's choice: never synthesize a trap. */
int feraiseexcept(int excepts)
{
	unsigned char env[28] = { 0 };
	excepts &= FE_ALL_EXCEPT;
	__asm__ __volatile__("fnstenv (%0)" : : "r"(env) : "memory");
	env[4] = (unsigned char)(env[4] | excepts);
	__asm__ __volatile__("fldenv (%0)" : : "r"(env) : "memory");
#ifndef __i386__
	{
		unsigned int mxcsr = 0;
		__asm__ __volatile__("stmxcsr (%0)" : : "r"(&mxcsr) : "memory");
		mxcsr |= (unsigned int)excepts;
		__asm__ __volatile__("ldmxcsr (%0)" : : "r"(&mxcsr) : "memory");
	}
#endif
	return 0;
}

int fetestexcept(int excepts)
{
	unsigned short sw = 0;
	excepts &= FE_ALL_EXCEPT;
	__asm__ __volatile__("fnstsw (%0)" : : "r"(&sw) : "memory");
#ifndef __i386__
	{
		unsigned int mxcsr = 0;
		__asm__ __volatile__("stmxcsr (%0)" : : "r"(&mxcsr) : "memory");
		return (int)((sw | mxcsr) & (unsigned int)excepts);
	}
#else
	return sw & excepts;
#endif
}

int fegetexceptflag(fexcept_t *flagp, int excepts)
{
	*flagp = (fexcept_t)fetestexcept(excepts);
	return 0;
}

int fesetexceptflag(const fexcept_t *flagp, int excepts)
{
	excepts &= FE_ALL_EXCEPT;
	(void)feclearexcept(~*flagp & excepts);
	(void)feraiseexcept(*flagp & excepts);
	return 0;
}

int fegetround(void)
{
	unsigned short cw = 0;
	__asm__ __volatile__("fnstcw (%0)" : : "r"(&cw) : "memory");
	return cw & 0xc00;
}

int fesetround(int round)
{
	unsigned short cw = 0;
	if (round != FE_TONEAREST && round != FE_DOWNWARD &&
	    round != FE_UPWARD && round != FE_TOWARDZERO)
		return -1;
	__asm__ __volatile__("fnstcw (%0)" : : "r"(&cw) : "memory");
	cw = (unsigned short)((cw & ~0xc00) | round);
	__asm__ __volatile__("fldcw (%0)" : : "r"(&cw) : "memory");
#ifndef __i386__
	{
		unsigned int mxcsr = 0;
		__asm__ __volatile__("stmxcsr (%0)" : : "r"(&mxcsr) : "memory");
		mxcsr = (mxcsr & ~0x6000u) | ((unsigned int)round << 3);
		__asm__ __volatile__("ldmxcsr (%0)" : : "r"(&mxcsr) : "memory");
	}
#endif
	return 0;
}

int fegetenv(fenv_t *envp)
{
	__asm__ __volatile__("fnstenv (%0)" : : "r"(envp->__x87env) : "memory");
#ifndef __i386__
	__asm__ __volatile__("stmxcsr (%0)" : : "r"(&envp->__mxcsr) : "memory");
#endif
	return 0;
}

int fesetenv(const fenv_t *envp)
{
	/* Default environment: round-to-nearest, all exceptions masked and
	 * clear, 0x37f/0x1f80 are the values a fresh x87/SSE unit powers up
	 * with (and what feclearexcept/fnclex alone cannot reproduce, since
	 * they leave the control word and rounding mode untouched). */
	if (envp == FE_DFL_ENV) {
		unsigned char env[28] = { 0 };
		unsigned short cw = 0x37f;
		env[0] = (unsigned char)cw;
		env[1] = (unsigned char)(cw >> 8);
		/* tag word (byte offset 8-9): 0xffff marks all eight x87
		 * registers empty, the real power-on/reset value -- 0 would
		 * mean every register holds a valid (garbage) value. */
		env[8] = 0xff;
		env[9] = 0xff;
		__asm__ __volatile__("fldenv (%0)" : : "r"(env) : "memory");
#ifndef __i386__
		{
			unsigned int mxcsr = 0x1f80;
			__asm__ __volatile__("ldmxcsr (%0)" : : "r"(&mxcsr) : "memory");
		}
#endif
		return 0;
	}
	__asm__ __volatile__("fldenv (%0)" : : "r"(envp->__x87env) : "memory");
#ifndef __i386__
	__asm__ __volatile__("ldmxcsr (%0)" : : "r"(&envp->__mxcsr) : "memory");
#endif
	return 0;
}

int feholdexcept(fenv_t *envp)
{
	(void)fegetenv(envp);
	(void)feclearexcept(FE_ALL_EXCEPT);
	return 0;
}

int feupdateenv(const fenv_t *envp)
{
	int ex = fetestexcept(FE_ALL_EXCEPT);
	(void)fesetenv(envp);
	(void)feraiseexcept(ex);
	return 0;
}
