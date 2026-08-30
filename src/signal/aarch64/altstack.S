/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __sig_call_on_altstack(void *sp, void (*fn)(void *), void *arg) --
 * call fn(arg) with the stack pointer moved to sp, then put it back.
 * aarch64 counterpart of src/signal/x86_64/altstack.S -- see that
 * file's own banner for why this exists at all (this library's signal
 * delivery is synchronous, so SA_ONSTACK is just a stack switch around
 * the call site, not a kernel-built sigreturn frame).
 *
 * AAPCS64: sp arrives in x0, fn in x1, arg in x2 -- this library's own
 * argument order, unlike the x86_64 file's Win64 renaming (sp/rcx,
 * fn/rdx, arg/r8), needs no shuffling since AAPCS64's first three
 * integer argument registers are already x0/x1/x2. x29 (frame pointer)
 * is callee-saved, so parking the real sp there across the call is
 * both safe and gives an unwinder something to follow -- the same
 * shape as a normal AAPCS64 prologue/epilogue, not a special trick.
 *
 * The 16-byte alignment mask (below) needs a general register, not sp,
 * as AND's own operand: register 31 means XZR in AND's encoding, not
 * SP, unlike ADD/SUB/MOV where it does -- `and sp, sp, #-16` does not
 * assemble at all (confirmed empirically). sp must be 16-byte aligned
 * at any AAPCS64 call boundary regardless, and this is what makes a
 * caller-supplied ss_sp+ss_size safe to use as a stack top whatever
 * its own alignment -- no Win64-style shadow space needed here, that
 * convention does not exist on this ABI.
 */
	.text
	.globl __sig_call_on_altstack
	.type __sig_call_on_altstack,%function
__sig_call_on_altstack:
	stp	x29, x30, [sp, #-16]!
	mov	x29, sp			/* park the real stack in a callee-saved reg */
	mov	x8, x0			/* x8 = altstack top (scratch, used immediately) */
	mov	x9, x1			/* x9 = fn (scratch, used immediately) */
	mov	x0, x2			/* arg -> first parameter */
	and	x8, x8, #-16		/* 16-byte align before the call */
	mov	sp, x8			/* switch: sp is the alternate stack top */
	blr	x9
	mov	sp, x29			/* back to the real stack */
	ldp	x29, x30, [sp], #16
	ret
	.size __sig_call_on_altstack, . - __sig_call_on_altstack
