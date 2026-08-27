/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * longjmp for i386 Windows (cdecl): restore what setjmp saved and resume
 * at the saved eip with the saved esp, making the setjmp call appear to
 * return val, or 1 if val is 0.  siglongjmp first restores a mask saved
 * by sigsetjmp.
 */

#ifdef __leading_underscore
# define _(s) _##s
#else
# define _(s) s
#endif

.text
.globl _(longjmp), _(_longjmp), _(siglongjmp)
_(siglongjmp):
	push	8(%esp)			/* preserve val */
	push	8(%esp)			/* env, now displaced by one push */
	call	_(__siglongjmp_restore)
	add	$4,%esp
	pop	%eax			/* val */
	mov	4(%esp),%edx		/* env */
	jmp	1f
_(longjmp):
_(_longjmp):
	mov	4(%esp),%edx		/* env */
	mov	8(%esp),%eax		/* val */
1:
	test	%eax,%eax
	jnz	2f
	inc	%eax			/* longjmp(env, 0) returns 1 */
2:	mov	(%edx),%ebx
	mov	4(%edx),%esi
	mov	8(%edx),%edi
	mov	12(%edx),%ebp
	mov	16(%edx),%esp
	jmp	*20(%edx)
