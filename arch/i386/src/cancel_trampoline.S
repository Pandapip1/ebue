/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifdef __leading_underscore
# define _(s) _##s
#else
# define _(s) s
#endif

.text
.globl _(__pthread_cancel_trampoline)
_(__pthread_cancel_trampoline):
	/* Context redirection is not a call, so normalize the interrupted
	 * stack before entering the non-returning cancellation path. */
	and	$-16,%esp
	call	_(__pthread_cancel_current)
	ud2
