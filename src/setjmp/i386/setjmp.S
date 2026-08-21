/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * setjmp for i386 Windows (cdecl).  jmp_buf.__jb holds ebx, esi, edi,
 * ebp, esp, eip in that order; esp is saved as it will be after our ret
 * (argument still in place, return address popped).  sigsetjmp saves no
 * signal mask, so all three entry points share one body; sigsetjmp's
 * extra argument is simply ignored.
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
	mov	4(%esp),%eax		/* env */
	mov	%ebx,(%eax)
	mov	%esi,4(%eax)
	mov	%edi,8(%eax)
	mov	%ebp,12(%eax)
	lea	4(%esp),%ecx		/* esp after our ret */
	mov	%ecx,16(%eax)
	mov	(%esp),%ecx		/* return address */
	mov	%ecx,20(%eax)
	xor	%eax,%eax
	ret
