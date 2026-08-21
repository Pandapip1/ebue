/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <time.h>
#include <limits.h>
#include <errno.h>
#include "time_impl.h"

struct tm *gmtime_r(const time_t *restrict tp, struct tm *restrict result)
{
	long long t = *tp;
	long long days = t / 86400;
	long long rem = t % 86400;
	long long y;
	unsigned m, d;

	if (rem < 0) { rem += 86400; days--; }
	__civil_from_days(days, &y, &m, &d);
	if (y - 1900 < INT_MIN || y - 1900 > INT_MAX) { errno = EOVERFLOW; return NULL; }

	result->tm_year = (int)(y - 1900);
	result->tm_mon = (int)m - 1;
	result->tm_mday = (int)d;
	result->tm_hour = (int)(rem / 3600);
	result->tm_min = (int)(rem % 3600 / 60);
	result->tm_sec = (int)(rem % 60);
	result->tm_wday = __wday_from_days(days);
	result->tm_yday = (int)(days - __days_from_civil(y, 1, 1));
	result->tm_isdst = 0;
	result->__tm_gmtoff = 0;
	result->__tm_zone = "UTC";
	return result;
}

struct tm *gmtime(const time_t *tp)
{
	static struct tm tm;
	return gmtime_r(tp, &tm);
}
