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

	days = __days_from_civil(y, m, 1) + (tm->tm_mday - 1);
	secs = days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
	t = (time_t)(secs + timezone);        /* local -> UTC */

	localtime_r(&t, tm);
	return t;
}
