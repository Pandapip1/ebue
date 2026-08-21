/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * longjmp for i386 Windows (cdecl): restore what setjmp saved and resume
 * at the saved eip with the saved esp, making the setjmp call appear to
 * return val, or 1 if val is 0.  siglongjmp is identical because
 * sigsetjmp saves no mask.
 */

#ifdef __leading_underscore
# define _(s) _##s
#else
# define _(s) s
#endif

.text
.globl _(longjmp), _(_longjmp), _(siglongjmp)
_(longjmp):
_(_longjmp):
_(siglongjmp):
	mov	4(%esp),%edx		/* env */
	mov	8(%esp),%eax		/* val */
	test	%eax,%eax
	jnz	1f
	inc	%eax			/* longjmp(env, 0) returns 1 */
1:	mov	(%edx),%ebx
	mov	4(%edx),%esi
	mov	8(%edx),%edi
	mov	12(%edx),%ebp
	mov	16(%edx),%esp
	jmp	*20(%edx)
