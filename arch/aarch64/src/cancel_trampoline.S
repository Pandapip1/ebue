/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * aarch64's half of arch/{i386,x86_64}/src/cancel_trampoline.S's split:
 * the landing pad __plat_thread_redirect_ip() forces a suspended
 * thread's PC to on async cancellation. Currently unreachable
 * (src/thread/linux/plat_thread.c's __plat_thread_redirect_ip() always
 * fails on this platform), but must stay a real, correct landing pad
 * for when that lands.
 *
 * Arch-keyed, not platform-keyed, so this compiles for both Linux
 * (clang) and NT/WOA (tcc) builds of ARCH=aarch64. The trailing trap is
 * #ifdef'd because tcc's arm64-win32 assembler does not implement BRK;
 * PLATFORM=nt substitutes an equivalent infinite self-branch instead.
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
