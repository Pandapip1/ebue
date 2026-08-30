/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * setjmp for aarch64 Linux. AAPCS64 makes x19-x28, x29 (frame pointer),
 * sp and the low 64 bits of v8-v15 (d8-d15) callee-saved; jmp_buf.__jb
 * holds, in order: x19 x20 x21 x22 x23 x24 x25 x26 x27 x28 x29 x30
 * (indices 0-11, byte offsets 0-88), sp (index 12, offset 96), then
 * d8-d15 (offsets 104-160, 8 bytes each -- only the low 64 bits of
 * each 128-bit v register need saving, since AAPCS64 only guarantees
 * the low half survives a call). x30 (the link register) is saved at
 * offset 88 as the "return address" slot, matching src/setjmp/x86_64/
 * setjmp.S's rip slot -- longjmp resumes there via a plain `ret`,
 * which on this arch implicitly branches to whatever is in x30.
 *
 * env arrives in x0, savemask (sigsetjmp only) in x1 -- AAPCS64's own
 * first two integer argument registers, so unlike the x86_64 file
 * (Win64 arguments arrive in rcx/rdx, this library's own convention
 * elsewhere always speaks of "env"/"savemask" positionally) no
 * register renaming is needed to match __sigsetjmp_save's C signature.
 *
 * sigsetjmp calls the C helper (src/setjmp/sigsetjmp.c) before joining
 * the shared capture body; x0 (env) and the original x30 (this
 * function's own return address, clobbered by the `bl` itself) are
 * parked on a small stack frame across that call and restored before
 * falling through, exactly mirroring the x86_64 file's rcx/rip
 * handling for the same reason.
 */
	.text
	.globl setjmp
	.globl _setjmp
	.globl sigsetjmp
	.type setjmp,%function
	.type _setjmp,%function
	.type sigsetjmp,%function
sigsetjmp:
	sub	sp, sp, #32
	stp	x0, x30, [sp]
	bl	__sigsetjmp_save
	ldp	x0, x30, [sp]
	add	sp, sp, #32
	b	1f
setjmp:
_setjmp:
1:
	stp	x19, x20, [x0, #0]
	stp	x21, x22, [x0, #16]
	stp	x23, x24, [x0, #32]
	stp	x25, x26, [x0, #48]
	stp	x27, x28, [x0, #64]
	stp	x29, x30, [x0, #80]
	mov	x8, sp			/* sp as it will be after our ret */
	str	x8, [x0, #96]
	stp	d8, d9, [x0, #104]
	stp	d10, d11, [x0, #120]
	stp	d12, d13, [x0, #136]
	stp	d14, d15, [x0, #152]
	mov	x0, #0
	ret
	.size setjmp, . - setjmp
