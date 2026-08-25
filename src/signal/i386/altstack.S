/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __sig_call_on_altstack(void *sp, void (*fn)(void *), void *arg) for
 * i386.  See the x86_64 file beside this one for what it is for; this
 * differs only in the calling convention.
 *
 * cdecl: the three arguments are on the caller's stack at 8/12/16(%ebp).
 * They must be loaded into registers BEFORE %esp moves, because once it
 * does they are no longer addressable off the new stack.
 *
 * Alignment: %esp must be 16-byte aligned immediately before the call so
 * the callee sees %esp+4 aligned.  The `sub $12` plus the 4-byte push of
 * arg is what lands it there.
 */

#ifdef __leading_underscore
# define _(s) _##s
#else
# define _(s) s
#endif

.text
.globl _(__sig_call_on_altstack)
_(__sig_call_on_altstack):
	push	%ebp
	mov	%esp,%ebp
	mov	8(%ebp),%eax		/* sp  -- read all three before %esp moves */
	mov	12(%ebp),%edx		/* fn  */
	mov	16(%ebp),%ecx		/* arg */
	mov	%eax,%esp		/* switch to the alternate stack */
	and	$-16,%esp
	sub	$12,%esp
	push	%ecx			/* arg, landing %esp 16-byte aligned */
	call	*%edx
	mov	%ebp,%esp		/* back to the real stack */
	pop	%ebp
	ret
