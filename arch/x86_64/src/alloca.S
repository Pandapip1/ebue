/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * alloca for tcc on x86_64 Windows.  Called as a normal function with
 * the size in %rcx; extend the stack below the return address, touching
 * each page for the guard page mechanism, and return the new space.
 */

#ifdef __leading_underscore
# define _(s) _##s
#else
# define _(s) s
#endif

.text
.globl _(alloca)
_(alloca):
	pop	%rdx			/* return address */
	mov	%rcx,%rax
	add	$15,%rax
	and	$-16,%rax		/* round up to 16 */
	jz	2f
1:	cmp	$4096,%rax
	jb	3f
	test	%rax,-4096(%rsp)	/* touch the next page */
	sub	$4096,%rsp
	sub	$4096,%rax
	jmp	1b
3:	sub	%rax,%rsp
	mov	%rsp,%rax
2:	push	%rdx
	ret
