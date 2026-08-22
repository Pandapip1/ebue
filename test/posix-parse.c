/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Boundary behaviour of the numeric/time parsers, checked against
 * POSIX.1-2017 (strtol.html, mktime.html, strftime.html).
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <string.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* strtol.html ERRORS: "If the correct value is outside the range of
 * representable values, {LONG_MIN}, {LONG_MAX}, ... shall be returned
 * ... and errno set to [ERANGE]." */
static void test_strtol_overflow(void)
{
	char *end;

	errno = 0;
	CHECK(strtol("99999999999999999999", &end, 10) == LONG_MAX && errno == ERANGE);
	errno = 0;
	CHECK(strtol("-99999999999999999999", &end, 10) == LONG_MIN && errno == ERANGE);
	errno = 0;
	CHECK(strtoul("99999999999999999999", &end, 10) == ULONG_MAX && errno == ERANGE);
	errno = 0;
	CHECK(strtoll("99999999999999999999999999999999", &end, 10) == LLONG_MAX && errno == ERANGE);
	errno = 0;
	CHECK(strtoll("-99999999999999999999999999999999", &end, 10) == LLONG_MIN && errno == ERANGE);
	errno = 0;
	CHECK(strtoull("99999999999999999999999999999999", &end, 10) == ULLONG_MAX && errno == ERANGE);

	/* exact boundary values must NOT set ERANGE */
	errno = 0;
	CHECK(strtol("2147483647", &end, 10) == 2147483647L && errno == 0);
}

/* strtol.html: "If the subject sequence is empty or does not have the
 * expected form, no conversion is performed; the value of nptr shall
 * be stored in the object pointed to by endptr". */
static void test_strtol_no_conversion(void)
{
	char *end;
	const char *s;

	s = "   xyz";
	errno = 0;
	CHECK(strtol(s, &end, 10) == 0 && end == s);  /* endptr == nptr, unchanged by leading spaces */

	s = "";
	end = NULL;
	CHECK(strtol(s, &end, 10) == 0 && end == s);

	/* "0x" with no hex digit after it: the "0x" prefix is only skipped
	 * when a valid hex digit follows it (src/stdlib/strtol.c requires
	 * isxdigit(s[2])), so here the subject sequence is just the leading
	 * "0" and the 'x' is left unconsumed -- this matches glibc. */
	s = "0x";
	end = NULL;
	CHECK(strtol(s, &end, 16) == 0 && end == s + 1);

	/* a valid prefix followed by junk: endptr points at the junk, not nptr */
	s = "123abc";
	end = NULL;
	CHECK(strtol(s, &end, 10) == 123 && end == s + 3);
}

/* mktime.html: "the other components shall be set to represent the
 * specified time since the Epoch, but with their values forced to the
 * ranges indicated in the <time.h> entry" -- out-of-range fields
 * normalize instead of erroring. */
static void test_mktime_normalize(void)
{
	struct tm tm;

	/* month 13 (0-based tm_mon range is 0-11): tm_mon=12 means "month 13"
	 * -> January of the next year. */
	memset(&tm, 0, sizeof tm);
	tm.tm_year = 100; tm.tm_mon = 12; tm.tm_mday = 1;  /* 2001-01-01 spelled as month 13 of 2000 */
	CHECK(mktime(&tm) != (time_t)-1);
	CHECK(tm.tm_year == 101 && tm.tm_mon == 0 && tm.tm_mday == 1);

	/* day 0 of March -> last day of February. 2000 is a leap year. */
	memset(&tm, 0, sizeof tm);
	tm.tm_year = 100; tm.tm_mon = 2; tm.tm_mday = 0;
	CHECK(mktime(&tm) != (time_t)-1);
	CHECK(tm.tm_year == 100 && tm.tm_mon == 1 && tm.tm_mday == 29);

	/* negative month: tm_mon = -1 -> December of the previous year. */
	memset(&tm, 0, sizeof tm);
	tm.tm_year = 101; tm.tm_mon = -1; tm.tm_mday = 1;
	CHECK(mktime(&tm) != (time_t)-1);
	CHECK(tm.tm_year == 100 && tm.tm_mon == 11 && tm.tm_mday == 1);

	/* mktime.html: "If the time since the Epoch cannot be represented,
	 * the function shall return the value (time_t)-1" -- a caller
	 * cannot always distinguish this from a real result of -1 (the
	 * second before the Epoch, UTC-adjacent); the standard error idiom
	 * is to clear errno first and see whether mktime sets it, but
	 * mktime is not required to set errno at all, so we only check
	 * that a genuinely unrepresentable date does not silently succeed
	 * with a plausible-looking time_t on a 32-bit time_t build. This
	 * check is skipped on a 64-bit time_t, where every tm year the
	 * struct's `int tm_year` can name is representable. */
	if (sizeof(time_t) == 4) {
		memset(&tm, 0, sizeof tm);
		tm.tm_year = 300000000; tm.tm_mon = 0; tm.tm_mday = 1;  /* far beyond 2038 */
		CHECK(mktime(&tm) == (time_t)-1);
	}
}

/* strftime.html: "Otherwise, 0 shall be returned and the contents of
 * the array are unspecified." when the result (with terminating NUL)
 * does not fit in maxsize. */
static void test_strftime_short_buffer(void)
{
	struct tm tm;
	char buf[11];  /* exactly "%Y-%m-%d\0" size, so maxsize<=11 never overflows it */
	size_t n;

	memset(&tm, 0, sizeof tm);
	tm.tm_year = 123; tm.tm_mon = 5; tm.tm_mday = 15;  /* 2023-06-15 */
	tm.tm_hour = 13; tm.tm_min = 5; tm.tm_sec = 9;

	/* "%Y-%m-%d" needs 11 bytes (10 + NUL): fits in a big buffer. */
	{
		char big[32];
		n = strftime(big, sizeof big, "%Y-%m-%d", &tm);
		CHECK(n == 10 && !strcmp(big, "2023-06-15"));
	}
	/* Too small by one byte (need 11, have 10): must return 0. */
	n = strftime(buf, 10, "%Y-%m-%d", &tm);
	CHECK(n == 0);
	/* Exactly enough (11, including the NUL): must succeed. */
	n = strftime(buf, 11, "%Y-%m-%d", &tm);
	CHECK(n == 10 && !strcmp(buf, "2023-06-15"));
	/* maxsize == 0: must return 0 without touching the array. */
	n = strftime(buf, 0, "%Y-%m-%d", &tm);
	CHECK(n == 0);
}

int main(void)
{
	test_strtol_overflow();
	test_strtol_no_conversion();
	test_mktime_normalize();
	test_strftime_short_buffer();

	if (!fails) printf("posix-parse: all tests passed\n");
	return fails != 0;
}
