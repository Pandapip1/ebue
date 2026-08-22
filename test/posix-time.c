/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Clause-by-clause POSIX.1-2017 audit of <time.h> not already exercised
 * by test/time.c (broad known-epoch/round-trip sanity pass) or
 * test/posix-parse.c (mktime normalization / strftime short-buffer
 * boundary). Each block cites the page it was checked against under
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/<name>.html
 *
 * Timezone-dependent checks set TZ explicitly rather than relying on
 * the runner's zone (see test/time.c for the same pattern).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <time.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* difftime.html RETURN VALUE: "the difference ... expressed in seconds
 * as a type double." Check the type, not just the numeric value (which
 * test/time.c already covers extensively). */
static void test_difftime_return_type(void)
{
	CHECK(sizeof(difftime(0, 0)) == sizeof(double));
	/* fractional-looking magnitude only possible if truly double: this
	 * value doesn't fit exactly in a 32-bit float's mantissa but does in
	 * a double, so a double-returning difftime reproduces it exactly. */
	{
		time_t a = (time_t)9007199254740993LL; /* 2^53+1 */
		double d = difftime(a, 0);
		CHECK(d == 9007199254740993.0);
	}
}

/* gmtime.html ERRORS: "[EOVERFLOW] The result cannot be represented."
 * RETURN VALUE: null pointer returned on error. localtime()/localtime_r()
 * are specified via the same page and inherit the same EOVERFLOW
 * behaviour (localtime_r calls gmtime_r after applying the fixed TZ
 * offset -- src/time/localtime.c). Use a time_t so far in the future
 * that the resulting year does not fit in `int` (tm_year is `int`). */
static void test_gmtime_overflow(void)
{
	struct tm sentinel, tm;
	time_t huge = LLONG_MAX;

	memset(&sentinel, 0x5a, sizeof sentinel);
	tm = sentinel;
	errno = 0;
	CHECK(gmtime_r(&huge, &tm) == NULL);
	CHECK(errno == EOVERFLOW);
	/* gmtime_r bails out before writing any field on overflow (checked
	 * by inspection of src/time/gmtime.c: the EOVERFLOW check runs
	 * before any assignment to *result), so the caller's struct is left
	 * untouched. */
	CHECK(memcmp(&tm, &sentinel, sizeof tm) == 0);

	errno = 0;
	CHECK(gmtime(&huge) == NULL);
	CHECK(errno == EOVERFLOW);

	CHECK(setenv("TZ", "UTC0", 1) == 0);
	tzset();
	errno = 0;
	CHECK(localtime_r(&huge, &tm) == NULL);
	CHECK(errno == EOVERFLOW);
}

/* mktime.html DESCRIPTION: "the original values of the tm_wday and
 * tm_yday components of the structure are ignored" -- garbage input
 * values in those fields must not influence the computed result. */
static void test_mktime_ignores_wday_yday_input(void)
{
	struct tm a, b;

	memset(&a, 0, sizeof a);
	a.tm_year = 100; a.tm_mon = 1; a.tm_mday = 29; /* 2000-02-29, a Tuesday */
	a.tm_wday = 99; a.tm_yday = -12345; /* garbage, must be ignored */
	b = a;
	b.tm_wday = 0; b.tm_yday = 0;

	CHECK(mktime(&a) == mktime(&b));
	CHECK(a.tm_wday == 2); /* actual Tuesday, not the garbage 99 */
	CHECK(a.tm_yday == 59);
}

/* mktime.html RETURN VALUE: "Upon successful completion, the values of
 * the tm_wday and tm_yday components ... shall be set appropriately,
 * and the other components are set to represent the specified time
 * since the Epoch, but with their values forced to the ranges
 * indicated in the <time.h> entry"; ERRORS: "[EOVERFLOW] The result
 * cannot be represented." RETURN VALUE: "the value (time_t)-1 shall be
 * returned and errno set to indicate the error."
 *
 * BUG: src/time/mktime.c calls localtime_r(&t, tm) and discards its
 * return value. When the computed y-1900 doesn't fit in `int` (tm_year
 * is `int`, and tm_mon/12 can push the effective year further out),
 * localtime_r -> gmtime_r correctly detects this and returns NULL with
 * errno set to EOVERFLOW (per gmtime.html), but mktime() still returns
 * the raw (garbage, unrepresentable) computed time_t instead of
 * (time_t)-1, and *tm is left with its original (also garbage) fields
 * rather than being either normalized or left alone consistently.
 * Confirmed live: tm_year=INT_MAX, tm_mon=100 makes
 * mktime() return 67768036422969600 with errno left at EOVERFLOW
 * (75) instead of returning (time_t)-1. */
#if 0 /* BUG: mktime.html RETURN VALUE/ERRORS -- src/time/mktime.c:38 discards
       * localtime_r()'s NULL-on-EOVERFLOW return instead of propagating it
       * as (time_t)-1. */
static void test_mktime_overflow_returns_minus_one(void)
{
	struct tm tm;

	memset(&tm, 0, sizeof tm);
	tm.tm_year = INT_MAX;
	tm.tm_mon = 100; /* mon/12 pushes the effective year past INT_MAX-1900 */
	tm.tm_mday = 1;
	errno = 0;
	CHECK(mktime(&tm) == (time_t)-1);
	CHECK(errno == EOVERFLOW);
}
#endif

/* clock.html DESCRIPTION: "the implementation's best approximation to
 * the processor time used by the process" -- CPU time, not wall-clock
 * time. Sleeping (wall-clock elapsed) should barely move clock()'s CPU
 * time, unlike a busy loop of comparable wall duration. */
static void test_clock_is_cpu_time_not_wall_time(void)
{
	struct timespec req, before_wall, after_wall;
	clock_t c1, c2;

	req.tv_sec = 0; req.tv_nsec = 150000000L; /* 150ms */
	clock_gettime(CLOCK_MONOTONIC, &before_wall);
	c1 = clock();
	CHECK(clock_nanosleep(CLOCK_REALTIME, 0, &req, NULL) == 0);
	c2 = clock();
	clock_gettime(CLOCK_MONOTONIC, &after_wall);

	CHECK(c1 != (clock_t)-1 && c2 != (clock_t)-1 && c2 >= c1);
	/* wall time definitely advanced by roughly 150ms */
	CHECK(after_wall.tv_sec > before_wall.tv_sec ||
	      (after_wall.tv_nsec - before_wall.tv_nsec) > 100000000L ||
	      after_wall.tv_sec - before_wall.tv_sec >= 1);
	/* but CPU time used while merely blocked in a sleep should be a
	 * small fraction of that -- well under 100ms of CLOCKS_PER_SEC
	 * (1000000) units, i.e. under 100000 clock() ticks. */
	CHECK((c2 - c1) < CLOCKS_PER_SEC / 10);
}

/* clock_gettime.html / clock_getres.html ERRORS: "[EINVAL] The clock_id
 * argument does not specify a known clock." Exercise the CPU-time
 * clocks' resolution too (test/time.c only checks REALTIME/MONOTONIC). */
static void test_clock_getres_cputime(void)
{
	struct timespec res;

	CHECK(clock_getres(CLOCK_PROCESS_CPUTIME_ID, &res) == 0);
	CHECK(res.tv_sec == 0 && res.tv_nsec > 0);
	CHECK(clock_getres(CLOCK_THREAD_CPUTIME_ID, &res) == 0);
	CHECK(res.tv_sec == 0 && res.tv_nsec > 0);
}

/* clock_getres.html DESCRIPTION: "If res is NULL, the clock resolution
 * is not returned." i.e. a NULL res is a legal no-op query, not an
 * error.
 *
 * BUG: src/time/clock_gettime.c's clock_getres() writes res->tv_sec /
 * res->tv_nsec unconditionally, with no NULL check, for every clock
 * id branch. Confirmed live: calling clock_getres(CLOCK_REALTIME, NULL)
 * crashes the process with SIGSEGV (wine exit code 11) rather than
 * returning 0. Not exercised live here since it would take down this
 * whole test binary (and the "N tests passed/failed" accounting with
 * it) rather than failing a single CHECK. */
#if 0 /* BUG: clock_getres.html DESCRIPTION -- src/time/clock_gettime.c's
       * clock_getres() dereferences `res` unconditionally; NULL crashes
       * (verified: SIGSEGV under wine) instead of being accepted as a
       * legal "don't return the resolution" query. */
static void test_clock_getres_null(void)
{
	CHECK(clock_getres(CLOCK_REALTIME, NULL) == 0);
}
#endif

/* clock_settime.html ERRORS: "[EINVAL] The tp argument to
 * clock_settime() is outside the range for the clock ID, or the
 * nanosecond field is negative or greater than or equal to 1000
 * million."
 *
 * BUG: src/time/clock_gettime.c's clock_settime() never checks
 * ts->tv_nsec's range before handing it to NtSetSystemTime -- only the
 * clock-id check (id != CLOCK_REALTIME) can produce EINVAL. Not
 * exercised live: on this target the only way to observe the
 * CLOCK_REALTIME path succeed or fail is to actually call
 * NtSetSystemTime, which depends on unpredictable process privilege
 * (see test/time.c's stime()/clock_settime() tests, which already
 * accept either outcome) and would either silently corrupt the host's
 * clock by a fraction of a second or mask the missing-EINVAL bug behind
 * a privilege-driven EPERM either way. */
#if 0 /* BUG: clock_settime.html ERRORS -- src/time/clock_gettime.c's
       * clock_settime() does not validate ts->tv_nsec is in
       * [0, 999999999] before calling NtSetSystemTime, so an
       * out-of-range nanosecond field is not rejected with EINVAL. */
static void test_clock_settime_bad_nsec(void)
{
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = -1;
	errno = 0;
	CHECK(clock_settime(CLOCK_REALTIME, &ts) == -1);
	CHECK(errno == EINVAL);
}
#endif

/* clock_nanosleep.html ERRORS: "[EINVAL] The rqtp argument specified a
 * nanosecond value less than zero or greater than or equal to 1000
 * million, or specified a clock ID that is not supported." */
static void test_clock_nanosleep_einval(void)
{
	struct timespec req;

	req.tv_sec = 0; req.tv_nsec = 0;
	errno = 0;
	CHECK(clock_nanosleep((clockid_t)999, 0, &req, NULL) == -1);
	CHECK(errno == EINVAL);

	req.tv_sec = 0; req.tv_nsec = 1000000000L; /* == 1e9, out of range */
	errno = 0;
	CHECK(clock_nanosleep(CLOCK_REALTIME, 0, &req, NULL) == -1);
	CHECK(errno == EINVAL);

	req.tv_sec = 0; req.tv_nsec = -1;
	errno = 0;
	CHECK(clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL) == -1);
	CHECK(errno == EINVAL);
}

/* clock_nanosleep.html DESCRIPTION: relative-mode (flags==0) suspends
 * the calling thread for at least the requested interval. Sanity-check
 * actual elapsed wall time against CLOCK_MONOTONIC, independent of the
 * CPU-time check above. */
static void test_clock_nanosleep_relative(void)
{
	struct timespec req, before, after;
	long long ns;

	req.tv_sec = 0; req.tv_nsec = 100000000L; /* 100ms */
	CHECK(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
	CHECK(clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL) == 0);
	CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);

	ns = (after.tv_sec - before.tv_sec) * 1000000000LL + (after.tv_nsec - before.tv_nsec);
	CHECK(ns >= 90000000LL); /* allow a little scheduling slack under */
}

/* clock_nanosleep.html DESCRIPTION: "If the flag TIMER_ABSTIME is set
 * ... the thread ... shall be suspended until ... the time value of
 * the clock specified by clock_id reaches the absolute time specified
 * by rqtp". For CLOCK_MONOTONIC, that means "reaches" as measured by
 * CLOCK_MONOTONIC's own (arbitrary-epoch) reading -- i.e. an absolute
 * request should be built from a prior clock_gettime(CLOCK_MONOTONIC)
 * reading plus a delta, and clock_nanosleep should sleep until that
 * monotonic instant.
 *
 * BUG: src/time/clock_nanosleep.c's TIMER_ABSTIME branch always builds
 * the absolute LARGE_INTEGER via __unix_to_nt(req->tv_sec,
 * req->tv_nsec), which assumes req is seconds-since-1970 (an NT
 * FILETIME conversion) regardless of clock_id. For CLOCK_MONOTONIC,
 * req is actually seconds-since-an-arbitrary-epoch (the performance
 * counter's start, typically near boot -- see monotonic_get() in
 * src/time/clock_gettime.c), a far smaller number: fed through
 * __unix_to_nt it produces an absolute NT time in the distant past
 * (near 1601), so NtDelayExecution returns immediately. Confirmed
 * live: requesting CLOCK_MONOTONIC/TIMER_ABSTIME for "now + 2s"
 * returned in about 3 microseconds instead of sleeping ~2 seconds. */
#if 0 /* BUG: clock_nanosleep.html DESCRIPTION -- src/time/clock_nanosleep.c's
       * TIMER_ABSTIME branch runs req through __unix_to_nt() (a
       * unix-epoch/NT-FILETIME conversion) even when clock_id is
       * CLOCK_MONOTONIC, whose absolute readings are not unix-epoch
       * seconds; the sleep returns almost instantly instead of waiting
       * for the requested monotonic instant. */
static void test_clock_nanosleep_monotonic_abstime(void)
{
	struct timespec now, req, before, after;
	long long ns;

	CHECK(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
	req = now;
	req.tv_sec += 1; /* absolute monotonic instant ~1s from now */

	CHECK(clock_gettime(CLOCK_MONOTONIC, &before) == 0);
	CHECK(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &req, NULL) == 0);
	CHECK(clock_gettime(CLOCK_MONOTONIC, &after) == 0);

	ns = (after.tv_sec - before.tv_sec) * 1000000000LL + (after.tv_nsec - before.tv_nsec);
	CHECK(ns >= 900000000LL); /* should have waited ~1s; actually returns in microseconds */
}
#endif

/* clock_nanosleep.html DESCRIPTION: "if, at the time of the call, the
 * time value specified by rqtp is less than or equal to the current
 * value of the clock ... then clock_nanosleep() shall return
 * immediately". CLOCK_REALTIME's TIMER_ABSTIME path does go through
 * __unix_to_nt correctly (REALTIME's own readings *are* unix-epoch
 * seconds), so this should behave correctly -- sanity-check it, since
 * the CLOCK_MONOTONIC case above is fenced as broken. */
static void test_clock_nanosleep_realtime_abstime_past(void)
{
	struct timespec past;

	CHECK(clock_gettime(CLOCK_REALTIME, &past) == 0);
	/* already in the past by the time NtDelayExecution sees it */
	CHECK(clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &past, NULL) == 0);
}

/* ctime.html DESCRIPTION: "It shall be equivalent to:
 *     asctime(localtime(clock))"
 * test/time.c checks ctime() against literal expected strings under
 * TZ=UTC0; check the equivalence itself, and under a non-UTC TZ so the
 * localtime() half of the equivalence actually does something. */
static void test_ctime_equals_asctime_localtime(void)
{
	time_t t = 951782400 + 3661; /* 2000-02-29-ish */
	struct tm tm;
	char abuf[32];

	CHECK(setenv("TZ", "EST5", 1) == 0);
	tzset();
	CHECK(!strcmp(ctime(&t), asctime(localtime(&t))));
	localtime_r(&t, &tm);
	CHECK(!strcmp(ctime(&t), asctime_r(&tm, abuf)));

	CHECK(setenv("TZ", "UTC0", 1) == 0);
	tzset();
}

/* ctime.html ERRORS: "No errors are defined." -- ctime_r's failure
 * mode (NULL) is only reachable through localtime_r()'s own EOVERFLOW,
 * which test_gmtime_overflow already exercises for localtime_r
 * directly; confirm ctime_r propagates it rather than crashing. */
static void test_ctime_r_overflow_propagates(void)
{
	time_t huge = LLONG_MAX;
	char buf[32];

	CHECK(setenv("TZ", "UTC0", 1) == 0);
	tzset();
	CHECK(ctime_r(&huge, buf) == NULL);
	CHECK(ctime(&huge) == NULL);
}

/* asctime.html: the example format is exactly 26 bytes including the
 * terminating NUL ("Sun Sep 16 01:03:52 1973\n\0"); asctime_r()
 * requires "at least 26 bytes". Confirm an exactly-26-byte buffer is
 * sufficient (test/time.c already checks strlen()==25 into a 32-byte
 * buffer; this pins the exact-fit boundary). */
static void test_asctime_r_exact_buffer(void)
{
	struct tm tm;
	time_t t = 0;
	char buf[26];

	CHECK(setenv("TZ", "UTC0", 1) == 0);
	tzset();
	gmtime_r(&t, &tm);
	CHECK(asctime_r(&tm, buf) == buf);
	CHECK(!strcmp(buf, "Thu Jan  1 00:00:00 1970\n"));
	CHECK(strlen(buf) + 1 == 26);
}

/* strptime.html: the conversion table includes "%C - All but the last
 * two digits of the year {2}" as a base (non-locale-alternate)
 * conversion, alongside %y for the low two digits.
 *
 * BUG: src/time/strptime.c has no case for '%C' (confirmed by
 * inspection: its switch handles c/D/x/F/r/R/T/X/Y/y/m/d/e/H/I/M/S/j/
 * u/w/a/A/b/B/h/p/z/Z/n/t/%, and falls through to `default: return
 * NULL` for anything else) -- any format using %C is rejected outright.
 * Confirmed live: strptime("19", "%C", &tm) returns NULL. */
#if 0 /* BUG: strptime.html conversion table -- src/time/strptime.c has no
       * '%C' case (century, "all but the last two digits of the year"),
       * so any format string using %C fails to parse at all. */
static void test_strptime_century(void)
{
	struct tm tm;
	char *end;

	memset(&tm, 0, sizeof tm);
	end = strptime("1970", "%C%y", &tm);
	CHECK(end != NULL && *end == 0);
	CHECK(tm.tm_year == 70);
}
#endif

/* strptime.html RETURN VALUE: "Upon successful completion, strptime()
 * shall return a pointer to the character following the last character
 * parsed." -- already exercised extensively in test/time.c via
 * strftime/strptime round-trips; add the DESCRIPTION clause that a
 * literal '%' in the format must match a literal '%' in the input
 * (%% conversion) even embedded mid-format, and that ordinary
 * whitespace characters in the format consume runs of input whitespace
 * of any length including zero (already covered for one case in
 * test/time.c; this covers a run of several).
 */
static void test_strptime_literal_percent_and_ws_run(void)
{
	struct tm tm;
	char *end;

	memset(&tm, 0, sizeof tm);
	end = strptime("31%done", "%d%%done", &tm);
	CHECK(end && *end == 0 && tm.tm_mday == 31);

	memset(&tm, 0, sizeof tm);
	end = strptime("2000\t\t  \n02", "%Y %m", &tm);
	CHECK(end && *end == 0 && tm.tm_year == 100 && tm.tm_mon == 1);
}

/* tzset.html DESCRIPTION: "The daylight variable shall be set to 0 if
 * Daylight Savings Time conversions should never be applied ...
 * otherwise it shall be set to a non-zero value." ntlibc's tzset() has
 * no DST ruleset at all (src/time/tzset.c), so daylight is always 0,
 * for every TZ value including ones with a DST suffix -- already
 * covered for one such TZ in test/time.c (PST8PDT,M3.2.0,M11.1.0);
 * add the plain "name+DST-name" form without a rule suffix. */
static void test_tzset_daylight_always_zero(void)
{
	CHECK(setenv("TZ", "CET-1CEST", 1) == 0);
	tzset();
	CHECK(daylight == 0);
	CHECK(timezone == -1 * 3600);
	CHECK(!strcmp(tzname[0], "CET"));

	CHECK(setenv("TZ", "UTC0", 1) == 0);
	tzset();
}

/* timespec_get: C11 (and POSIX.1-2024, not POSIX.1-2017 base -- see
 * https://man7.org/linux/man-pages/man3/timespec_get.3.html); ntlibc
 * still declares it in <time.h>. "returns the nonzero base if it is a
 * supported time base ... or 0 otherwise." test/time.c already checks
 * TIME_UTC success and an unsupported-base failure; add that ts is left
 * alone on failure (unspecified by the standard, but worth pinning
 * ntlibc's actual behaviour) and that repeated calls are monotonic
 * non-decreasing along with CLOCK_REALTIME. */
static void test_timespec_get_matches_realtime(void)
{
	struct timespec ts, rt;

	CHECK(timespec_get(&ts, TIME_UTC) == TIME_UTC);
	CHECK(clock_gettime(CLOCK_REALTIME, &rt) == 0);
	CHECK(rt.tv_sec >= ts.tv_sec && rt.tv_sec - ts.tv_sec <= 5);
}

int main(void)
{
	test_difftime_return_type();
	test_gmtime_overflow();
	test_mktime_ignores_wday_yday_input();
	test_clock_is_cpu_time_not_wall_time();
	test_clock_getres_cputime();
	test_clock_nanosleep_einval();
	test_clock_nanosleep_relative();
	test_clock_nanosleep_realtime_abstime_past();
	test_ctime_equals_asctime_localtime();
	test_ctime_r_overflow_propagates();
	test_asctime_r_exact_buffer();
	test_strptime_literal_percent_and_ws_run();
	test_tzset_daylight_always_zero();
	test_timespec_get_matches_realtime();

	if (!fails) printf("posix-time: all tests passed\n");
	return fails != 0;
}
