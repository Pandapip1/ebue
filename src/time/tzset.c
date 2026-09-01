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
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- GNU feature-test macro has its specified reserved spelling
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

int daylight;
long timezone;
char *tzname[2] = { (char *)"UTC", (char *)"UTC" };

static char __tzname_std[32] = "UTC"; // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- libc-internal name is intentionally reserved against application collision
static char __tzname_dst[32] = "UTC"; // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- libc-internal name is intentionally reserved against application collision

static void read_name(const char **input, char *out, size_t cap)
{
	const char *p = *input;
	size_t n = 0;

	if (*p == '<') {
		p++;
		while (*p && *p != '>') {
			if (n + 1 < cap) out[n++] = *p;
			p++;
		}
		if (*p == '>') p++;
	} else {
		while (isalpha((unsigned char)*p)) {
			if (n + 1 < cap) out[n++] = *p;
			p++;
		}
	}
	out[n] = 0;
	*input = p;
}

void tzset(void)
{
	const char *tz = getenv("TZ");
	long h = 0, mn = 0, s = 0;
	int sign = 1;

	daylight = 0;
	if (!tz || !*tz) {
		memcpy(__tzname_std, "UTC", sizeof "UTC");
		memcpy(__tzname_dst, "UTC", sizeof "UTC");
		timezone = 0;
		tzname[0] = __tzname_std;
		tzname[1] = __tzname_dst;
		return;
	}

	/* Name: a run of letters, or a "quoted" run of anything but '>'. */
	read_name(&tz, __tzname_std, sizeof __tzname_std);
	if (!__tzname_std[0]) memcpy(__tzname_std, "UTC", sizeof "UTC");
	tzname[0] = __tzname_std;

	/* Offset: [+-]?H[:MM[:SS]], POSIX sense (added to local time to get
	 * UTC), same sign convention as the `timezone` global. */
	if (*tz == '+') tz++;
	else if (*tz == '-') { sign = -1; tz++; }
	if (isdigit((unsigned char)*tz)) {
		h = strtol(tz, (char **)&tz, 10);
		if (*tz == ':') { tz++; mn = strtol(tz, (char **)&tz, 10); }
		if (*tz == ':') { tz++; s = strtol(tz, (char **)&tz, 10); }
	}
	read_name(&tz, __tzname_dst, sizeof __tzname_dst);
	if (!__tzname_dst[0])
		(void)strlcpy(__tzname_dst, __tzname_std, sizeof __tzname_dst);
	tzname[1] = __tzname_dst;
	/* Combined in `long long`, not in `long`.  h, mn and s come out of
	 * strtol(), which saturates at LONG_MAX -- 2147483647 on this
	 * LLP64 target -- and `h * 3600` at that value overflows a 32-bit
	 * `long`: undefined behaviour, reachable from nothing more exotic
	 * than TZ=X2147483647 in the environment, which is to say from
	 * whoever started the process.  The product needs 43 bits and gets
	 * them; the result is then clamped rather than wrapped, so every
	 * TZ whose offset already fitted keeps exactly the value it had
	 * and only the ones that did not fit change -- from undefined to
	 * saturated.
	 *
	 * Found by fuzz/fuzz_time.c, which cannot see the overflow itself:
	 * `long` is 64 bits in the native fuzzing build and 32 on the
	 * target, so UBSan never fires there.  What it checks instead is
	 * the value -- that whatever tzset() computes is representable in
	 * the target's `long` -- and TZ=X9999999999999999 produced
	 * 7730941129200 for a field that holds at most 2147483647. */
	{
		long long total = (long long)h * 3600 + (long long)mn * 60 + (long long)s;
		if (sign < 0) total = -total;
		if (total > LONG_MAX) total = LONG_MAX;
		/* localtime_r publishes -timezone in the same signed-long
		 * representation.  Keep the negative endpoint symmetric so that
		 * inverse is representable too; LONG_MIN has no positive mate. */
		else if (total < -LONG_MAX) total = -LONG_MAX;
		timezone = (long)total;
	}
}
