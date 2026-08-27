/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * longjmp for x86_64 Windows: restore what setjmp saved (see setjmp.S
 * for the layout) and resume at the saved rip with the saved rsp, making
 * the setjmp call appear to return val, or 1 if val is 0.  env in %rcx,
 * val in %rdx.  siglongjmp first restores a mask saved by sigsetjmp.
 */

#ifdef __leading_underscore
# define _(s) _##s
#else
# define _(s) s
#endif

.text
.globl _(longjmp), _(_longjmp), _(siglongjmp)
_(siglongjmp):
	sub	$56,%rsp
	mov	%rcx,32(%rsp)
	mov	%rdx,40(%rsp)
	call	_(__siglongjmp_restore)
	mov	32(%rsp),%rcx
	mov	40(%rsp),%rdx
	add	$56,%rsp
	jmp	1f
_(longjmp):
_(_longjmp):
1:
	mov	(%rcx),%rbx
	mov	8(%rcx),%rbp
	mov	16(%rcx),%rsi
	mov	24(%rcx),%rdi
	mov	32(%rcx),%r12
	mov	40(%rcx),%r13
	mov	48(%rcx),%r14
	mov	56(%rcx),%r15
	movups	96(%rcx),%xmm6
	movups	112(%rcx),%xmm7
	.byte	0x44,0x0f,0x10,0x81	/* movups 128(%rcx),%xmm8 */
	.long	128
	.byte	0x44,0x0f,0x10,0x89	/* movups 144(%rcx),%xmm9 */
	.long	144
	.byte	0x44,0x0f,0x10,0x91	/* movups 160(%rcx),%xmm10 */
	.long	160
	.byte	0x44,0x0f,0x10,0x99	/* movups 176(%rcx),%xmm11 */
	.long	176
	.byte	0x44,0x0f,0x10,0xa1	/* movups 192(%rcx),%xmm12 */
	.long	192
	.byte	0x44,0x0f,0x10,0xa9	/* movups 208(%rcx),%xmm13 */
	.long	208
	.byte	0x44,0x0f,0x10,0xb1	/* movups 224(%rcx),%xmm14 */
	.long	224
	.byte	0x44,0x0f,0x10,0xb9	/* movups 240(%rcx),%xmm15 */
	.long	240
	mov	64(%rcx),%rsp
	mov	%edx,%eax
	test	%eax,%eax
	jnz	2f
	inc	%eax			/* longjmp(env, 0) returns 1 */
2:	jmp	*72(%rcx)
