/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _SYS_TIME_H
#define _SYS_TIME_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <sys/select.h>

/* tz is accepted-and-ignored, matching glibc's own "obsolete" treatment. */
int gettimeofday (struct timeval *__restrict, void *__restrict) __attribute__((nonnull(1)));

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

struct itimerval {
	struct timeval it_interval;
	struct timeval it_value;
};

int getitimer (int, struct itimerval *);  /* undefined-ok: an interval timer
	is defined by its repeat, and SIGALRM on NT arrives through an APC that
	only runs while the thread is in an alertable wait, so expiries a
	computing thread missed coalesce instead of queueing.
	ITIMER_VIRTUAL/ITIMER_PROF need CPU-time signals this platform has no
	scheduler tick to generate either. Linux has both a real signal-delivery
	model and this library's own repeating software timer machinery, and
	defines this one (src/time/linux/plat_itimer.c). */
int setitimer (int, const struct itimerval *__restrict, struct itimerval *__restrict);  /* undefined-ok: see getitimer */
int utimes (const char *, const struct timeval [2]);

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
struct timezone {
	int tz_minuteswest;
	int tz_dsttime;
};
int futimesat(int, const char *, const struct timeval [2]);
/* tz is accepted-and-ignored, same as gettimeofday()'s. */
int settimeofday(const struct timeval *, const struct timezone *) __attribute__((nonnull(1)));
int adjtime (const struct timeval *, struct timeval *);  /* undefined-ok:
	adjtime() means a gradual slew, applied a little at a time; the only
	clock-setting primitive available on NT (NtSetSystemTime) is a single
	hard jump, with no W32Time-style slew API reachable from a plain
	process. Linux has a real NTP-style slewing API (adjtimex(2)) and
	defines this one, in src/time/linux/plat_adjtime.c. */
#define timerisset(t) ((t)->tv_sec || (t)->tv_usec)
#define timerclear(t) ((t)->tv_sec = (t)->tv_usec = 0)
#define timercmp(s,t,op) ((s)->tv_sec == (t)->tv_sec ? \
	(s)->tv_usec op (t)->tv_usec : (s)->tv_sec op (t)->tv_sec)
#define timeradd(s,t,a) (void) ( (a)->tv_sec = (s)->tv_sec + (t)->tv_sec, \
	((a)->tv_usec = (s)->tv_usec + (t)->tv_usec) >= 1000000 && \
	((a)->tv_usec -= 1000000, (a)->tv_sec++) )
#define timersub(s,t,a) (void) ( (a)->tv_sec = (s)->tv_sec - (t)->tv_sec, \
	((a)->tv_usec = (s)->tv_usec - (t)->tv_usec) < 0 && \
	((a)->tv_usec += 1000000, (a)->tv_sec--) )
#endif

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
