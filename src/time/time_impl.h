/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Bits shared between the files in src/time/ but not meant for anyone
 * else: the proleptic-Gregorian civil calendar <-> day-count conversion
 * (there is no other libc around to borrow it from, so it is reproduced
 * here), the day/month name tables strftime/strptime/asctime need, and
 * a tiny zero-padded-decimal formatter used by both strftime and
 * asctime so the two agree on how numbers look.
 */
#ifndef _NTLIBC_TIME_IMPL_H
#define _NTLIBC_TIME_IMPL_H

#include <time.h>

/* Howard Hinnant's days_from_civil/civil_from_days
 * (http://howardhinnant.github.io/date_algorithms.html), which turn a
 * proleptic-Gregorian y/m/d (m in 1..12) into a day count relative to
 * 1970-01-01 and back, correctly and branchlessly for any year an int
 * can hold (including negative ones, via the usual "add 400 years"
 * trick to keep the truncating divisions well-behaved). */
static inline long long __days_from_civil(long long y, unsigned m, unsigned d)
{
	y -= m <= 2;
	long long era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned)(y - era * 400);                /* 0..399 */
	unsigned mp = m + (m > 2 ? -3u : 9u);                     /* Mar=0..Feb=11 */
	unsigned doy = (153 * mp + 2) / 5 + d - 1;                /* 0..365 */
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;     /* 0..146096 */
	return era * 146097 + (long long)doe - 719468;
}

static inline void __civil_from_days(long long z, long long *y, unsigned *m, unsigned *d)
{
	z += 719468;
	long long era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned)(z - era * 146097);              /* 0..146096 */
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* 0..399 */
	long long yy = (long long)yoe + era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);   /* 0..365 */
	unsigned mp = (5 * doy + 2) / 153;                        /* Mar=0..Feb=11 */
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp + (mp < 10 ? 3 : -9);
	*y = yy + (*m <= 2);
}

/* 1970-01-01 (day 0) was a Thursday (wday 4, Sunday=0); the +11 turns
 * C's truncating (possibly negative) % into the usual 0..6 result. */
static inline int __wday_from_days(long long z)
{
	return (int)((z % 7 + 11) % 7);
}

extern const char *const __ntlibc_day_name[7];
extern const char *const __ntlibc_day_name_abbr[7];
extern const char *const __ntlibc_month_name[12];
extern const char *const __ntlibc_month_name_abbr[12];

/* Write the decimal digits of v into tmp (capacity cap), zero-padded (or
 * pad-charred) to at least width digits -- more if v needs them, never
 * truncated.  Returns the digit count written, least-significant-digit
 * bookkeeping done internally; the caller gets the digits in the right
 * (most-significant-first) order in tmp[0..return). */
static inline int __num_digits(char *tmp, int cap, unsigned long v, int width, char pad)
{
	char rev[24];
	int n = 0;
	do {
		if (n < (int)sizeof rev) rev[n] = (char)('0' + v % 10);
		n++;
		v /= 10;
	} while (v);
	while (n < width) {
		if (n < (int)sizeof rev) rev[n] = pad;
		n++;
	}
	if (n > cap) n = cap;
	for (int i = 0; i < n; i++) tmp[i] = rev[n - 1 - i];
	return n;
}

#endif
