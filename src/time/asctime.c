/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * "Www Mmm dd hh:mm:ss yyyy\n" -- built by hand rather than through
 * sprintf, since stdio's formatted output isn't necessarily around yet
 * (this module doesn't want a dependency on it either way).
 */
#include <time.h>
#include <string.h>
#include "time_impl.h"

char *asctime_r(const struct tm *tm, char *buf)
{
	char *p = buf;
	int n;
	const char *wd = (unsigned)tm->tm_wday < 7 ? __ntlibc_day_name_abbr[tm->tm_wday] : "???";
	const char *mo = (unsigned)tm->tm_mon < 12 ? __ntlibc_month_name_abbr[tm->tm_mon] : "???";

	memcpy(p, wd, 3); p += 3; *p++ = ' ';
	memcpy(p, mo, 3); p += 3; *p++ = ' ';
	n = __num_digits(p, 2, (unsigned)tm->tm_mday, 2, ' '); p += n; *p++ = ' ';
	n = __num_digits(p, 2, (unsigned)tm->tm_hour, 2, '0'); p += n; *p++ = ':';
	n = __num_digits(p, 2, (unsigned)tm->tm_min, 2, '0'); p += n; *p++ = ':';
	n = __num_digits(p, 2, (unsigned)tm->tm_sec, 2, '0'); p += n; *p++ = ' ';
	n = __num_digits(p, 8, (unsigned long)(tm->tm_year + 1900), 4, '0'); p += n;
	*p++ = '\n';
	*p = 0;
	return buf;
}

char *asctime(const struct tm *tm)
{
	static char buf[32];
	return asctime_r(tm, buf);
}
