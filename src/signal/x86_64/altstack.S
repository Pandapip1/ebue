/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __sig_call_on_altstack(void *sp, void (*fn)(void *), void *arg) --
 * call fn(arg) with the stack pointer moved to sp, then put it back.
 *
 * This is what makes SA_ONSTACK mean something.  Delivery in this
 * library is synchronous (see src/signal/signal.c's header), so unlike a
 * kernel -- which has to build a signal frame on the alternate stack and
 * return through sigreturn -- we own the call site and an alternate
 * stack is just a stack switch around it.
 *
 * Windows x64 ABI: sp arrives in %rcx, fn in %rdx, arg in %r8.  rbp is
 * callee-saved, so parking the original %rsp there across the call is
 * safe and gives an unwinder something to follow.
 *
 * Alignment is load-bearing, not hygiene: %rsp must be 16-byte aligned
 * immediately before the call so the callee sees %rsp+8 aligned, and
 * Win64 requires 32 bytes of shadow space above it that the callee may
 * write without asking.  The `and` is what makes a caller-supplied
 * ss_sp+ss_size safe to use as a stack top whatever its alignment.
 */

#ifdef __leading_underscore
# define _(s) _##s
#else
# define _(s) s
#endif

.text
.globl _(__sig_call_on_altstack)
_(__sig_call_on_altstack):
	push	%rbp
	mov	%rsp,%rbp		/* park the real stack in a callee-saved reg */
	mov	%rcx,%rsp		/* switch: sp is the alternate stack top */
	and	$-16,%rsp		/* 16-byte align before the call */
	sub	$32,%rsp		/* Win64 shadow space for the callee */
	mov	%r8,%rcx		/* arg -> first parameter */
	call	*%rdx
	mov	%rbp,%rsp		/* back to the real stack */
	pop	%rbp
	ret
