/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stack probe for tcc on i386 Windows.  When a function's frame is 4096
 * bytes or more, tcc emits "mov $framesize,%eax; call __chkstk" as the
 * whole prologue, so this routine must both touch every page of the new
 * frame (Windows grows the stack one guard page at a time) and build the
 * frame itself: on return the caller expects its old %ebp saved on the
 * stack, %ebp pointing at it, and %esp lowered by the frame size.
 */

#ifdef __leading_underscore
# define _(s) _##s
#else
# define _(s) s
#endif

.text
.globl _(__chkstk)
_(__chkstk):
	/* Save the caller's %ebp where the return address sits, keeping
	   the return address in %ebp for the moment: the net effect is
	   the "push %ebp" of an ordinary prologue. */
	xchg	%ebp,(%esp)
	push	%ebp			/* return address */
	lea	4(%esp),%ebp		/* frame pointer -> saved %ebp */
	push	%ecx
	mov	%ebp,%ecx
1:	sub	$4096,%ecx
	test	%eax,(%ecx)		/* touch the page */
	sub	$4096,%eax
	cmp	$4096,%eax
	jge	1b
	sub	%eax,%ecx		/* the leftover, < one page */
	test	%eax,(%ecx)
	mov	%esp,%eax		/* %eax -> [saved ecx][ret] */
	mov	%ecx,%esp		/* frame established */
	mov	(%eax),%ecx
	jmp	*4(%eax)
