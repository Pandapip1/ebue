/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _SYS_TIME_H
#define _SYS_TIME_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <sys/select.h>

int gettimeofday (struct timeval *__restrict, void *__restrict);

#define ITIMER_REAL    0
#define ITIMER_VIRTUAL 1
#define ITIMER_PROF    2

struct itimerval {
	struct timeval it_interval;
	struct timeval it_value;
};

int getitimer (int, struct itimerval *);  /* undefined-ok: ITIMER_REAL needs
	SIGALRM delivery on expiry, and there is none to build on -- alarm()
	itself is already a permanent stub returning 0 (src/unistd/sleep.c),
	for the identical reason (see ualarm() in unistd.h). ITIMER_VIRTUAL/
	ITIMER_PROF fire on CPU time consumed rather than wall-clock time, an
	even harder signal to generate without a scheduler tick this library
	sees. A kernel32 timer queue (CreateTimerQueueTimer) does not change
	this: it would still need to deliver from a callback thread into
	__raise_internal()'s unlocked handlers/blocked/pending state, the
	same data race src/signal/signal.c's ctrl_handler() already flags as
	tolerable only because Ctrl-C is rare -- a real interval timer firing
	repeatedly is not */
int setitimer (int, const struct itimerval *__restrict, struct itimerval *__restrict);  /* undefined-ok: see getitimer */
int utimes (const char *, const struct timeval [2]);

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
struct timezone {
	int tz_minuteswest;
	int tz_dsttime;
};
int futimes(int, const struct timeval [2]);
int futimesat(int, const char *, const struct timeval [2]);
int lutimes(const char *, const struct timeval [2]);
int settimeofday(const struct timeval *, const struct timezone *);
int adjtime (const struct timeval *, struct timeval *);  /* undefined-ok:
	adjtime() means a *gradual* slew towards the target, applied a little
	at a time so nothing observes the clock jumping or running backwards.
	The only ntdll (or kernel32) primitive this library has for setting
	the clock at all is NtSetSystemTime (src/time/stime.c), a single hard
	jump; there is no W32Time-style slew API reachable from a plain
	process at either layer, so there is nothing to build the gradual
	half of adjtime() on */
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
