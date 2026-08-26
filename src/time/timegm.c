/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * timegm() is mktime()'s UTC-only cousin: no tzset()/timezone shift,
 * and gmtime_r rather than localtime_r normalizes *tm on the way out.
 */
#include <time.h>
#include "time_impl.h"

time_t timegm(struct tm *tm)
{
	long long y = (long long)tm->tm_year + 1900;
	long long mon = tm->tm_mon;
	long long days, secs;
	unsigned m;
	time_t t;

	y += mon / 12;
	mon %= 12;
	if (mon < 0) { mon += 12; y--; }
	m = (unsigned)mon + 1;

	/* tm_mday gets the same widening as tm_hour/tm_min below, and for
	 * the same reason: it is an `int` the caller is entitled to hand
	 * over out of range, and `tm->tm_mday - 1` is evaluated in `int`.
	 * At tm_mday == INT_MIN that subtraction is signed overflow --
	 * undefined behaviour inside the very function whose contract is
	 * that out-of-range fields are accepted and normalized.  Found by
	 * fuzz/fuzz_time.c; UBSan reported
	 * "signed integer overflow: -2147483648 - 1 cannot be represented
	 * in type 'int'" at this line. */
	days = __days_from_civil(y, m, 1) + ((long long)tm->tm_mday - 1);
	/* tm_hour/tm_min are `int`, unbounded like tm_mon above (timegm must
	 * accept and normalize out-of-range fields); widen before multiplying
	 * so a large caller-supplied value overflows in `long long`, not in
	 * `int`. */
	secs = days * 86400 + (long long)tm->tm_hour * 3600 + (long long)tm->tm_min * 60 + tm->tm_sec;
	t = (time_t)secs;

	gmtime_r(&t, tm);
	return t;
}
