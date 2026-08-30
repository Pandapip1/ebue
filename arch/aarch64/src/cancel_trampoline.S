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
	brk	#0   /* unreachable: __pthread_cancel_redirected is _Noreturn */
