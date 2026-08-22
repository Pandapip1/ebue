/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This target has no timezone database (no /usr/share/zoneinfo, no
 * Windows registry lookup implemented here), so "local time" can't mean
 * anything richer than a fixed UTC offset.  tzset() understands just
 * the numeric-offset prefix of a POSIX TZ string -- "EST5EDT" or
 * "PST8PDT7" style DST rules are read only insofar as parsing the
 * leading "name offset" stops harmlessly at the first character it
 * doesn't understand.  With no TZ set (or TZ=UTC/empty) local time is
 * UTC, which is the safe default for a system that can't otherwise
 * know better.  daylight is always 0: without rules there is no DST to
 * apply, ever.
 */
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int daylight;
long timezone;
char *tzname[2] = { (char *)"UTC", (char *)"UTC" };

static char __tzname_buf[32] = "UTC";

void tzset(void)
{
	const char *tz = getenv("TZ");
	size_t i = 0;
	long h = 0, mn = 0, s = 0;
	int sign = 1;

	daylight = 0;
	if (!tz || !*tz) {
		strcpy(__tzname_buf, "UTC");
		timezone = 0;
		tzname[0] = tzname[1] = __tzname_buf;
		return;
	}

	/* Name: a run of letters, or a "quoted" run of anything but '>'. */
	if (*tz == '<') {
		tz++;
		while (*tz && *tz != '>' && i < sizeof __tzname_buf - 1) __tzname_buf[i++] = *tz++;
		if (*tz == '>') tz++;
	} else {
		while (isalpha((unsigned char)*tz) && i < sizeof __tzname_buf - 1) __tzname_buf[i++] = *tz++;
	}
	__tzname_buf[i] = 0;
	if (!__tzname_buf[0]) strcpy(__tzname_buf, "UTC");
	tzname[0] = tzname[1] = __tzname_buf;

	/* Offset: [+-]?H[:MM[:SS]], POSIX sense (added to local time to get
	 * UTC), same sign convention as the `timezone` global. */
	if (*tz == '+') tz++;
	else if (*tz == '-') { sign = -1; tz++; }
	if (isdigit((unsigned char)*tz)) {
		h = strtol(tz, (char **)&tz, 10);
		if (*tz == ':') { tz++; mn = strtol(tz, (char **)&tz, 10); }
		if (*tz == ':') { tz++; s = strtol(tz, (char **)&tz, 10); }
	}
	timezone = sign * (h * 3600 + mn * 60 + s);
}
