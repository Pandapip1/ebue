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

	days = __days_from_civil(y, m, 1) + (tm->tm_mday - 1);
	secs = days * 86400 + tm->tm_hour * 3600 + tm->tm_min * 60 + tm->tm_sec;
	t = (time_t)secs;

	gmtime_r(&t, tm);
	return t;
}
