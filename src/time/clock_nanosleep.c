/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NtDelayExecution's LARGE_INTEGER is negative for "relative, N 100ns
 * units from now" and positive for "absolute, N 100ns units since the
 * 1601 epoch" -- exactly the TIMER_ABSTIME distinction, so a
 * CLOCK_REALTIME absolute request converts straight through
 * __unix_to_nt (its readings *are* unix-epoch seconds, see
 * realtime_get() in clock_gettime.c).
 *
 * CLOCK_MONOTONIC's absolute readings are not unix-epoch seconds --
 * they're seconds since the performance counter's arbitrary epoch (see
 * monotonic_get() in clock_gettime.c) -- and NtDelayExecution has no
 * "absolute performance-counter tick" mode at all, only "absolute NT
 * FILETIME" or "relative". So a CLOCK_MONOTONIC/TIMER_ABSTIME request
 * is measured out against a fresh clock_gettime(CLOCK_MONOTONIC)
 * reading, the same way musl's generic clock_nanosleep falls back for
 * clocks its host can't wait on absolutely, and turned into a relative
 * delay from here.
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
		if (id == CLOCK_MONOTONIC) {
			struct timespec now;
			long long delta_sec, delta_nsec, ticks;

			clock_gettime(CLOCK_MONOTONIC, &now);
			delta_sec = (long long)req->tv_sec - now.tv_sec;
			delta_nsec = (long long)req->tv_nsec - now.tv_nsec;
			if (delta_nsec < 0) { delta_nsec += 1000000000LL; delta_sec--; }
			if (delta_sec < 0) { delta_sec = 0; delta_nsec = 0; }   /* already due */

			ticks = delta_sec * __TICKS_PER_SEC + (delta_nsec + 99) / 100;
			t = -ticks;
			if (!t) t = -1;
		} else {
			t = __unix_to_nt(req->tv_sec, req->tv_nsec);
		}
	} else {
		t = -(req->tv_sec * __TICKS_PER_SEC + (req->tv_nsec + 99) / 100);
		if (!t) t = -1;
	}
	NtDelayExecution(0, &t);
	if (rem && !(flags & TIMER_ABSTIME)) rem->tv_sec = rem->tv_nsec = 0;
	return 0;
}
