/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * alloca for tcc on i386 Windows.  tcc calls this as a real cdecl
 * function but expects the callee to leave %esp lowered: pop the size
 * argument, extend the stack by that much (touching each page so the
 * guard page mechanism works), and return the new space, restoring the
 * argument slot so the caller's stack accounting stays right.
 */

#ifdef __leading_underscore
# define _(s) _##s
#else
# define _(s) s
#endif

.text
.globl _(alloca), _(__alloca)
_(alloca):
_(__alloca):
	pop	%edx			/* return address */
	pop	%eax			/* size */
	add	$3,%eax
	and	$-4,%eax		/* round up to 4 */
	jz	2f
1:	cmp	$4096,%eax
	jb	3f
	test	%eax,-4096(%esp)	/* touch the next page */
	sub	$4096,%esp
	sub	$4096,%eax
	jmp	1b
3:	sub	%eax,%esp
	mov	%esp,%eax
2:	push	%edx			/* dummy: refill the argument slot */
	push	%edx			/* return address */
	ret
