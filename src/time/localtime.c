/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * "Local" time is UTC shifted by the fixed offset tzset() parsed out of
 * TZ (see tzset.c for why there is no more to it than that on this
 * target).  `timezone` is POSIX's seconds-west-of-UTC, so local time is
 * UTC minus timezone; feeding that shifted instant to gmtime_r gets all
 * the field arithmetic (including the running-into-gmtime overflow
 * normalization) for free.
 */
#include <time.h>
#include "time_impl.h"

struct tm *localtime_r(const time_t *restrict tp, struct tm *restrict result)
{
	time_t t;

	tzset();
	t = *tp - timezone;
	if (!gmtime_r(&t, result)) return NULL;
	result->tm_isdst = 0;
	result->__tm_gmtoff = -timezone;
	result->__tm_zone = tzname[0];
	return result;
}

struct tm *localtime(const time_t *tp)
{
	static struct tm tm;
	return localtime_r(tp, &tm);
}
