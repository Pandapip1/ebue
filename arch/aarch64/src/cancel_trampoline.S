/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * aarch64's half of arch/{i386,x86_64}/src/cancel_trampoline.S's own
 * split: the landing pad __plat_thread_redirect_ip() forces a
 * suspended thread's PC to on async cancellation. See
 * src/thread/linux/plat_thread.c's own __plat_thread_redirect_ip():
 * on this platform it always fails (no real context-redirection
 * primitive built yet -- a disclosed gap, not this trampoline's
 * concern), so nothing jumps here in practice today. It still needs
 * to be a real, correct landing pad rather than an unimplemented
 * symbol: the moment a real __plat_thread_redirect_ip() lands for this
 * arch, this is the body it redirects into, unchanged.
 *
 * This file is arch-keyed (arch/aarch64/src/), not platform-keyed, so
 * the Makefile's ARCH_GLOBS compiles it unconditionally for every
 * platform using ARCH=aarch64 -- today that means both the Linux build
 * (clang) and the NT/WOA build (tcc). The trailing trap after the
 * _Noreturn call is therefore #ifdef'd: tcc's arm64-win32 assembler
 * (a real, current, empirically-probed limitation, not a permanent
 * ISA fact -- see src/math/aarch64_math.h's own __TINYC__ comment for
 * the full probe) does not implement BRK at all ("not implemented"),
 * so PLATFORM=nt substitutes an equivalent infinite self-branch: both
 * forms are unreachable in practice (nothing calls into this trampoline
 * yet, per this file's own banner above) and both trap execution in
 * place if that ever stops being true, differing only in whether a
 * debugger sees a hardware breakpoint or a spinning thread.
 */
.text
.globl __pthread_cancel_trampoline
__pthread_cancel_trampoline:
	/* Context redirection does not enter with any particular SP
	 * alignment guarantee. AAPCS64 requires 16-byte SP alignment at
	 * every public call boundary; realign through a GP register since
	 * AArch64's logical (AND) instructions cannot take SP as an
	 * operand directly (unlike ADD/SUB immediate forms). */
	mov	x9, sp
	and	x9, x9, #-16
	mov	sp, x9
	bl	__pthread_cancel_redirected
#ifdef __TINYC__
1:	b	1b   /* unreachable: __pthread_cancel_redirected is _Noreturn */
#else
	brk	#0   /* unreachable: __pthread_cancel_redirected is _Noreturn */
#endif
