/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* sched_yield.html: "The sched_yield() function shall force the running
 * thread to relinquish the processor until it again becomes the head of
 * its thread list."  RETURN VALUE -- "shall return 0 if it completes
 * successfully, or ... -1 and set errno".  ERRORS -- "No errors are
 * defined."
 *
 * NtYieldExecution() is the NTDLL primitive, and takes no arguments.
 * It returns STATUS_SUCCESS when it actually switched away, and the
 * informational (non-error, high bit clear) STATUS_NO_YIELD_PERFORMED
 * 0x40000024 when there was no other runnable thread to switch to --
 * this is what kernel32's SwitchToThread() turns into a FALSE return.
 *
 * That second case is *not* a POSIX failure: the spec's only
 * requirement is that the caller relinquish the processor, and having
 * relinquished it to a scheduler that immediately handed it back
 * satisfies that. POSIX defines no errors here at all, so there is no
 * errno value that could describe it either. Hence the unconditional
 * 0: the return value is checked for nothing because there is nothing
 * NtYieldExecution can report that POSIX would call a failure. */
#include <sched.h>
#include "libc.h"

int sched_yield(void)
{
	NtYieldExecution();
	return 0;
}
