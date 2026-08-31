/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef	_TIME_H
#define _TIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#ifdef __cplusplus
#define NULL 0L
#else
#define NULL ((void*)0)
#endif

#define __NEED_size_t
#define __NEED_time_t
#define __NEED_clock_t
#define __NEED_struct_timespec
#define __NEED_struct_tm

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)
#define __NEED_clockid_t
#define __NEED_timer_t
#define __NEED_pid_t
#define __NEED_locale_t
#endif

#include <bits/alltypes.h>


clock_t clock (void);
time_t time (time_t *);
double difftime (time_t, time_t);
/* tm is required: src/time/mktime.c dereferences tm->tm_year/tm_mon/
 * tm_mday/tm_hour/tm_min/tm_sec unconditionally to build the instant it
 * then hands to localtime_r(), with no NULL check anywhere. */
time_t mktime (struct tm *) __attribute__((nonnull(1)));
size_t strftime (char *__restrict, size_t, const char *__restrict, const struct tm *__restrict);
struct tm *gmtime (const time_t *);
struct tm *localtime (const time_t *);
char *asctime (const struct tm *);
char *ctime (const time_t *);
/* ts is required (undefined per ISO C's timespec_get if base is
 * unsupported the function returns 0 without touching ts, but every real
 * base -- TIME_UTC -- always writes ts->tv_sec/tv_nsec unconditionally,
 * src/time/timespec_get.c, and no caller in this tree ever passes NULL
 * ts, even together with an unsupported base (test/time.c's own
 * `timespec_get(&tg, 12345)` still passes a real buffer). */
int timespec_get(struct timespec *, int) __attribute__((nonnull(1)));

#define CLOCKS_PER_SEC ((clock_t)1000000)

#define TIME_UTC 1

#if defined(_POSIX_SOURCE) || defined(_POSIX_C_SOURCE) \
 || defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE)

size_t strftime_l (char *  __restrict, size_t, const char *  __restrict, const struct tm *  __restrict, locale_t);

/* tp/result are both required in every one of gmtime_r()/localtime_r()'s
 * own bodies: gmtime_r() dereferences *tp unconditionally to split it
 * into days/rem, then writes result->tm_year and every other field
 * unconditionally on the success path (src/time/gmtime.c) -- neither is
 * gated by a NULL check anywhere. localtime_r() dereferences *tp for its
 * own overflow check, then (after a successful gmtime_r() call, which
 * already requires result nonnull) writes result->tm_isdst/__tm_gmtoff/
 * __tm_zone unconditionally too (src/time/localtime.c) -- the checker's
 * own report only names *tp for each (it shows one finding per function),
 * but result's own direct field-store dereferences are exactly as real
 * and unconditional, verified by hand against both bodies. */
struct tm *gmtime_r (const time_t *__restrict, struct tm *__restrict)
    __attribute__((nonnull(1, 2)));
struct tm *localtime_r (const time_t *__restrict, struct tm *__restrict)
    __attribute__((nonnull(1, 2)));
/* tm/buf are both required: src/time/asctime.c's asctime_r() reads
 * tm->tm_wday/tm_mon/tm_mday/tm_hour/tm_min/tm_sec/tm_year unconditionally
 * to format them, and writes through buf (aliased as `p`) unconditionally
 * from the very first byte, with no NULL check on either -- the checker's
 * own report names only tm->tm_wday (one finding per function), but buf's
 * own `char *p = buf; ...; *p++ = ...;` writes are exactly as real,
 * verified by hand. */
char *asctime_r (const struct tm *__restrict, char *__restrict)
    __attribute__((nonnull(1, 2)));
char *ctime_r (const time_t *, char *);

void tzset (void);

struct itimerspec {
	struct timespec it_interval;
	struct timespec it_value;
};

#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3
#define CLOCK_MONOTONIC_RAW      4
#define CLOCK_REALTIME_COARSE    5
#define CLOCK_MONOTONIC_COARSE   6
#define CLOCK_BOOTTIME           7

#define TIMER_ABSTIME 1

int nanosleep (const struct timespec *, struct timespec *);
int clock_getres (clockid_t, struct timespec *);
/* clock_gettime()'s own body (src/time/clock_gettime.c) never dereferences
 * ts itself -- it only switches on id and forwards ts to realtime_get()/
 * monotonic_get()/cputime_get(), each of which is required (below in
 * this file's own translation unit; see that file's own static
 * declarations) and none of which is reached on an unrecognized id, so
 * ts is left unmarked here deliberately: there is nothing in
 * clock_gettime()'s OWN body for the attribute to describe, the same
 * "forwarded, callee already owns the contract" shape as ctime_r()
 * above. clock_settime()'s ts, by contrast, IS dereferenced directly in
 * its own body (`ts->tv_nsec` before anything else once id passes), so
 * it is marked. */
int clock_gettime (clockid_t, struct timespec *);
int clock_settime (clockid_t, const struct timespec *) __attribute__((nonnull(2)));
/* req is required (dereferenced directly, `req->tv_nsec`, as soon as id
 * passes its own check, with no NULL check of req anywhere); rem is
 * genuinely optional -- POSIX documents it as such ("if the rmtp
 * argument is non-NULL") and src/time/clock_nanosleep.c has a real,
 * live `if (rem && ...)` guard, the same shape as nanosleep()'s own
 * rem. */
int clock_nanosleep (clockid_t, int, const struct timespec *, struct timespec *)
    __attribute__((nonnull(3)));
/* id is required: src/time/clock_gettime.c's clock_getcpuclockid()
 * writes `*id = CLOCK_PROCESS_CPUTIME_ID;` unconditionally on its
 * success path, with no NULL check -- the ESRCH early return (pid
 * mismatch) never touches id either way, and no caller in this tree
 * ever passes NULL id together with a mismatched pid. */
int clock_getcpuclockid (pid_t, clockid_t *) __attribute__((nonnull(2)));
struct sigevent;
/* id is required: src/time/timer.c's timer_create() writes
 * `*id = (timer_t)timer;` unconditionally on its only success path, with
 * no NULL check anywhere. event is genuinely optional -- POSIX
 * documents "if evp is NULL" as a real, defined case (SIGALRM is used
 * instead), and every one of timer_create()'s own uses of it is guarded
 * by a live `if (event) ...`/`event ? ... :` check. */
int timer_create(clockid_t, struct sigevent *__restrict, timer_t *__restrict)
    __attribute__((nonnull(3)));
int timer_delete(timer_t);
int timer_getoverrun(timer_t);
int timer_gettime(timer_t, struct itimerspec *);
/* value is required: src/time/timer.c's timer_settime() dereferences
 * `&value->it_value`/`&value->it_interval` unconditionally, before the
 * timer is even looked up, with no NULL check anywhere. old is
 * genuinely optional -- POSIX documents "if ovalue is not NULL" as a
 * real, defined case, and timer_settime()'s own `if (old)
 * timer_value(timer, old);` is a live guard, not decoration. */
int timer_settime(timer_t, int, const struct itimerspec *__restrict, struct itimerspec *__restrict)
    __attribute__((nonnull(3)));

extern int daylight;
extern long timezone;
extern char *tzname[2];

#endif


#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
/* tm is required: src/time/strptime.c's strptime() itself dereferences
 * tm->tm_hour unconditionally on its own success path (the pm-adjustment
 * check, reached whether or not the format even contained a %p) with no
 * NULL check, and its own parse() helper writes through tm on every
 * recognized conversion with none either. s/f are deliberately NOT
 * marked here even though parse() requires both directly: strptime()'s
 * own body only ever forwards them into parse() without dereferencing
 * either itself (`s = parse(s, f, tm, ...)`), the same "forwarded,
 * callee already owns the contract" shape as ctime_r()/clock_gettime()
 * above. */
char *strptime (const char *__restrict, const char *__restrict, struct tm *__restrict)
    __attribute__((nonnull(3)));
extern int getdate_err;
struct tm *getdate (const char *);
#endif


#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
/* tp is required: src/time/stime.c dereferences *tp unconditionally as
 * its very first statement, with no NULL check. */
int stime(const time_t *) __attribute__((nonnull(1)));
/* tm is required: src/time/timegm.c dereferences tm->tm_year/tm_mon/
 * tm_mday/tm_hour/tm_min/tm_sec unconditionally, the same shape as
 * mktime()'s own tm above (this is its UTC-only cousin), with no NULL
 * check anywhere. */
time_t timegm(struct tm *) __attribute__((nonnull(1)));
#endif

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
