/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * setjmp for x86_64 Windows.  The Windows x64 ABI makes rbx, rbp, rsi,
 * rdi, r12-r15, rsp and xmm6-xmm15 callee-saved; jmp_buf.__jb holds, in
 * order: rbx rbp rsi rdi r12 r13 r14 r15 rsp rip (indices 0-9), and the
 * ten xmm registers from index 12 (byte offset 96), 16 bytes each.  The
 * buffer is only guaranteed 8-byte alignment, so unaligned stores are
 * used throughout.  tcc's assembler does not know xmm8-xmm15, so those
 * movups instructions are emitted as raw bytes (44 0f 11/10 modrm disp32).
 *
 * sigsetjmp saves no signal mask (there is none to save on NT), so all
 * three entry points share one body.  env arrives in %rcx, and for
 * sigsetjmp the (ignored) savemask in %rdx.
 */

#ifdef __leading_underscore
# define _(s) _##s
#else
# define _(s) s
#endif

.text
.globl _(setjmp), _(_setjmp), _(sigsetjmp)
_(setjmp):
_(_setjmp):
_(sigsetjmp):
	mov	%rbx,(%rcx)
	mov	%rbp,8(%rcx)
	mov	%rsi,16(%rcx)
	mov	%rdi,24(%rcx)
	mov	%r12,32(%rcx)
	mov	%r13,40(%rcx)
	mov	%r14,48(%rcx)
	mov	%r15,56(%rcx)
	lea	8(%rsp),%rax		/* rsp as it will be after our ret */
	mov	%rax,64(%rcx)
	mov	(%rsp),%rax		/* return address */
	mov	%rax,72(%rcx)
	movups	%xmm6,96(%rcx)
	movups	%xmm7,112(%rcx)
	.byte	0x44,0x0f,0x11,0x81	/* movups %xmm8,128(%rcx) */
	.long	128
	.byte	0x44,0x0f,0x11,0x89	/* movups %xmm9,144(%rcx) */
	.long	144
	.byte	0x44,0x0f,0x11,0x91	/* movups %xmm10,160(%rcx) */
	.long	160
	.byte	0x44,0x0f,0x11,0x99	/* movups %xmm11,176(%rcx) */
	.long	176
	.byte	0x44,0x0f,0x11,0xa1	/* movups %xmm12,192(%rcx) */
	.long	192
	.byte	0x44,0x0f,0x11,0xa9	/* movups %xmm13,208(%rcx) */
	.long	208
	.byte	0x44,0x0f,0x11,0xb1	/* movups %xmm14,224(%rcx) */
	.long	224
	.byte	0x44,0x0f,0x11,0xb9	/* movups %xmm15,240(%rcx) */
	.long	240
	xor	%eax,%eax
	ret
