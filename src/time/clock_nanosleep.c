/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NtDelayExecution's LARGE_INTEGER is negative for "relative, N 100ns
 * units from now" and positive for "absolute, N 100ns units since the
 * 1601 epoch" -- exactly the TIMER_ABSTIME distinction, so absolute
 * requests convert straight through __unix_to_nt instead of being
 * measured out against clock_gettime like the fallback in musl's
 * generic clock_nanosleep does.
 */
#include <time.h>
#include <errno.h>
#include "libc.h"

int clock_nanosleep(clockid_t id, int flags, const struct timespec *req, struct timespec *rem)
{
	LARGE_INTEGER t;

	if (id != CLOCK_REALTIME && id != CLOCK_MONOTONIC) { errno = EINVAL; return -1; }
	if (req->tv_nsec < 0 || req->tv_nsec >= 1000000000L || req->tv_sec < 0) { errno = EINVAL; return -1; }

	if (flags & TIMER_ABSTIME) {
		t = __unix_to_nt(req->tv_sec, req->tv_nsec);
	} else {
		t = -(req->tv_sec * __TICKS_PER_SEC + (req->tv_nsec + 99) / 100);
		if (!t) t = -1;
	}
	NtDelayExecution(0, &t);
	if (rem && !(flags & TIMER_ABSTIME)) rem->tv_sec = rem->tv_nsec = 0;
	return 0;
}
