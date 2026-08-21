/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stack probe for tcc on x86_64 Windows.  As on i386, tcc replaces the
 * whole prologue of a large-frame function with "mov $framesize,%eax;
 * call __chkstk": probe each page of the new frame, then leave the
 * caller's %rbp pushed, %rbp pointing at it, and %rsp lowered by the
 * (sign-extended 32-bit) frame size.
 */

#ifdef __leading_underscore
# define _(s) _##s
#else
# define _(s) s
#endif

.text
.globl _(__chkstk)
_(__chkstk):
	xchg	%rbp,(%rsp)		/* save caller's %rbp, fetch return address */
	push	%rbp			/* return address */
	lea	8(%rsp),%rbp		/* frame pointer -> saved %rbp */
	push	%rcx
	mov	%rbp,%rcx
	movslq	%eax,%rax
1:	sub	$4096,%rcx
	test	%rax,(%rcx)		/* touch the page */
	sub	$4096,%rax
	cmp	$4096,%rax
	jge	1b
	sub	%rax,%rcx
	test	%rax,(%rcx)
	mov	%rsp,%rax
	mov	%rcx,%rsp		/* frame established */
	mov	(%rax),%rcx
	jmp	*8(%rax)
