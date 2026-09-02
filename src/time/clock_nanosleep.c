/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * clock_nanosleep.html RETURN VALUE: "If the clock_nanosleep() function
 * returns because the requested time has elapsed, its return value
 * shall be zero. ... The clock_nanosleep() function shall fail if:
 * [...] These functions shall not set errno." -- clock_nanosleep(), like
 * the other pthread_*-shaped functions, returns the error NUMBER
 * directly rather than -1 with errno set. This file used to do the
 * latter (errno = EINVAL; return -1), which meant every caller checking
 * `clock_nanosleep(...) == EINVAL` -- the way OPTS's own
 * clock_nanosleep/11-1 and /13-1 do, straight out of the man page's own
 * example -- saw -1 instead and reported a bogus failure.
 *
 * It also called NtDelayExecution(0, ...) -- NOT alertable -- so a
 * signal could never interrupt the wait and [EINTR] could never be
 * produced, regardless of what arrived.  src/unistd/sleep.c's
 * __alertable_delay() is nanosleep()'s/sleep()'s real EINTR path
 * (alertable NtDelayExecution(1, ...), retried against the elapsed
 * ticks, testing __sig_caught_count() so an IGNORED signal does not end
 * the wait early -- see its own comment for the rest); this now shares
 * it instead of re-deriving a weaker version.
 *
 * NtDelayExecution's LARGE_INTEGER is negative for "relative, N 100ns
 * units from now" and positive for "absolute, N 100ns units since the
 * 1601 epoch" -- exactly the TIMER_ABSTIME distinction, so a
 * CLOCK_REALTIME absolute request converts straight through
 * __unix_to_ticks (its readings *are* unix-epoch seconds, see
 * realtime_get() in clock_gettime.c).  __alertable_delay only takes a
 * relative tick count, though, so an absolute request is turned into
 * one before the call in every case -- CLOCK_REALTIME included, via a
 * clock_gettime() reading -- rather than only for CLOCK_MONOTONIC.
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

int clock_nanosleep(clockid_t id, int flags, const struct timespec *req, struct timespec *rem) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	long long ticks, owed = 0;

	if (id != CLOCK_REALTIME && id != CLOCK_MONOTONIC) return EINVAL;
	if (req->tv_nsec < 0 || req->tv_nsec >= 1000000000L || req->tv_sec < 0) return EINVAL;

	if (flags & TIMER_ABSTIME) {
		struct timespec now;

		clock_gettime(id, &now);
		ticks = __timespec_diff_ticks(req->tv_sec, req->tv_nsec,
			now.tv_sec, now.tv_nsec);
	} else {
		ticks = __duration_ticks(req->tv_sec, req->tv_nsec);
	}

	if (__alertable_delay(ticks, &owed, "clock_nanosleep()") < 0) {
		/* "If clock_nanosleep() is interrupted by a signal ... and
		 * the rmtp argument is non-NULL ... the timespec structure
		 * ... is updated to contain the amount of time remaining ...
		 * This retention of the remaining time only applies if
		 * flags does not contain TIMER_ABSTIME." */
		if (rem && !(flags & TIMER_ABSTIME)) {
			rem->tv_sec = (time_t)(owed / __TICKS_PER_SEC);
			rem->tv_nsec = (long)(owed % __TICKS_PER_SEC) * 100;
		}
		return EINTR;
	}
	return 0;
}
