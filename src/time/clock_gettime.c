/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * CLOCK_REALTIME (and its coarse alias) is NtQuerySystemTime, the same
 * NT-epoch 100ns-tick clock time() and gmtime() are built on.
 *
 * CLOCK_MONOTONIC and friends want a clock that never jumps when
 * someone runs stime()/clock_settime(REALTIME): NtQueryPerformanceCounter
 * gives a free-running counter plus its frequency, which is exactly
 * that.  BOOTTIME is treated the same as MONOTONIC since this target
 * has no separate suspend-time accounting to add back in.
 *
 * The CPUTIME clocks read KERNEL_USER_TIMES for the current process
 * (there is no cheap equivalent for "this thread only" without
 * NtQueryInformationThread, which nt.h doesn't have wired up, so
 * THREAD_CPUTIME_ID reports the same thing PROCESS_CPUTIME_ID does --
 * an acceptable approximation on a target with no libc to compare
 * against, and still monotonic and CPU-time-like).
 */
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include "libc.h"
#include "plat_time.h"

static int realtime_get(struct timespec *ts)
{
	long long now;
	__plat_realtime_get(&now);
	ts->tv_sec = (time_t)__nt_to_unix_sec(now);
	ts->tv_nsec = __nt_to_unix_nsec(now);
	return 0;
}

static int monotonic_get(struct timespec *ts)
{
	long long count, freq;
	if (__plat_perfcounter_get(&count, &freq) < 0) return -1;
	if (!__clock_qpc_to_timespec(count, freq, &ts->tv_sec, &ts->tv_nsec)) {
		errno = EOVERFLOW;
		return -1;
	}
	return 0;
}

static int cputime_get(struct timespec *ts)
{
	long long kernel, user, ticks; /* 100ns units, kernel + user */
	if (__plat_process_cpu_ticks(&kernel, &user) < 0) return -1;
	if (!__clock_combine_cpu_ticks(kernel, user, &ticks)) {
		errno = EOVERFLOW;
		return -1;
	}
	ts->tv_sec = (time_t)(ticks / __TICKS_PER_SEC);
	ts->tv_nsec = (long)(ticks % __TICKS_PER_SEC) * 100;
	return 0;
}

int clock_gettime(clockid_t id, struct timespec *ts)
{
	switch (id) {
	case CLOCK_REALTIME:
	case CLOCK_REALTIME_COARSE:
		return realtime_get(ts);
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
	case CLOCK_MONOTONIC_COARSE:
	case CLOCK_BOOTTIME:
		return monotonic_get(ts);
	case CLOCK_PROCESS_CPUTIME_ID:
	case CLOCK_THREAD_CPUTIME_ID:
		return cputime_get(ts);
	default:
		errno = EINVAL;
		return -1;
	}
}

int clock_settime(clockid_t id, const struct timespec *ts)
{
	long long nt;

	if (id != CLOCK_REALTIME) { errno = EINVAL; return -1; }
	if (ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) { errno = EINVAL; return -1; }
	if (!__unix_to_nt(ts->tv_sec, ts->tv_nsec, &nt)) {
		errno = EINVAL;
		return -1;
	}
	return __plat_realtime_set(nt);
}

int clock_getres(clockid_t id, struct timespec *res)
{
	switch (id) {
	case CLOCK_REALTIME:
	case CLOCK_REALTIME_COARSE:
	case CLOCK_PROCESS_CPUTIME_ID:
	case CLOCK_THREAD_CPUTIME_ID:
		if (res) {
			res->tv_sec = 0;
			res->tv_nsec = 100;    /* NT's system clock ticks in 100ns units */
		}
		return 0;
	case CLOCK_MONOTONIC:
	case CLOCK_MONOTONIC_RAW:
	case CLOCK_MONOTONIC_COARSE:
	case CLOCK_BOOTTIME: {
		long long count, freq;
		if (!res) return 0;
		if (__plat_perfcounter_get(&count, &freq) < 0) return -1;
		if (!__clock_qpc_resolution(freq, &res->tv_sec, &res->tv_nsec)) {
			errno = EOVERFLOW;
			return -1;
		}
		return 0;
	}
	default:
		errno = EINVAL;
		return -1;
	}
}

int clock_getcpuclockid(pid_t pid, clockid_t *id)
{
	/* No handle-by-pid CPU-time clock without OpenProcess (and the
	 * ACCESS_DENIED that usually comes with querying another process's
	 * times); only "this process" is supported.  Unlike most POSIX
	 * interfaces this function returns an error number directly and
	 * does not report failure through errno. */
	if (pid != 0 && pid != getpid()) return ESRCH;
	*id = CLOCK_PROCESS_CPUTIME_ID;
	return 0;
}
