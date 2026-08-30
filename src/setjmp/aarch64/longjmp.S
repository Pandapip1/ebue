/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * longjmp for aarch64 Linux: restore what setjmp.S saved (see that
 * file for the layout) and resume at the saved x30 with the saved sp,
 * making the setjmp call appear to return val, or 1 if val is 0. env
 * in x0, val in x1 -- AAPCS64's own argument registers, no renaming
 * needed (see setjmp.S's own comment). siglongjmp first restores a
 * mask saved by sigsetjmp, parking x0/x1 across that call since both
 * are caller-saved and __siglongjmp_restore is an ordinary C call free
 * to clobber them.
 */
	.text
	.globl longjmp
	.globl _longjmp
	.globl siglongjmp
	.type longjmp,%function
	.type _longjmp,%function
	.type siglongjmp,%function
siglongjmp:
	sub	sp, sp, #16
	stp	x0, x1, [sp]
	bl	__siglongjmp_restore
	ldp	x0, x1, [sp]
	add	sp, sp, #16
	b	1f
longjmp:
_longjmp:
1:
	ldp	x19, x20, [x0, #0]
	ldp	x21, x22, [x0, #16]
	ldp	x23, x24, [x0, #32]
	ldp	x25, x26, [x0, #48]
	ldp	x27, x28, [x0, #64]
	ldp	x29, x30, [x0, #80]
	ldr	x8, [x0, #96]
	ldp	d8, d9, [x0, #104]
	ldp	d10, d11, [x0, #120]
	ldp	d12, d13, [x0, #136]
	ldp	d14, d15, [x0, #152]
	mov	sp, x8
	cmp	w1, #0
	csinc	w0, w1, wzr, ne		/* longjmp(env, 0) returns 1 */
	ret
	.size longjmp, . - longjmp
