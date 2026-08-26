/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * mktime() has to accept out-of-range fields (tm_mon == 13, tm_mday ==
 * 0, ...) and normalize them, and fill in tm_wday/tm_yday on the way
 * out.  The trick is to let the month spill into the year with plain
 * division/modulo *before* handing it to __days_from_civil (which only
 * promises the right answer for m in 1..12), compute the resulting
 * instant, then hand that straight back through localtime_r: it will
 * derive fully-normalized fields from the instant anyway, so there is
 * no separate normalization path to keep in sync with gmtime's.
 */
#include <time.h>
#include "time_impl.h"

time_t mktime(struct tm *tm)
{
	long long y = (long long)tm->tm_year + 1900;
	long long mon = tm->tm_mon;
	long long days, secs;
	unsigned m;
	time_t t;

	tzset();       /* so `timezone` below is current */
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
	/* tm_hour/tm_min are `int`, unbounded like tm_mon above (mktime must
	 * accept and normalize out-of-range fields); widen before multiplying
	 * so a large caller-supplied value overflows in `long long`, not in
	 * `int`. */
	secs = days * 86400 + (long long)tm->tm_hour * 3600 + (long long)tm->tm_min * 60 + tm->tm_sec;
	t = (time_t)(secs + timezone);        /* local -> UTC */

	if (!localtime_r(&t, tm)) return (time_t)-1;
	return t;
}
