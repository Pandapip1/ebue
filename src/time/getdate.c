/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Real getdate() reads the $DATEMSK file, a newline-separated list of
 * strptime templates, and tries each against its argument -- that is a
 * whole locale/template-file subsystem this target doesn't have (and
 * stdio here is minimal), so this is a pragmatic stand-in: it tries a
 * handful of the templates programs actually pass to getdate/date(1)
 * directly against the string, ignoring $DATEMSK entirely.  Fields the
 * matched template didn't set are left zeroed (1900-01-00) rather than
 * defaulting to "today" the way POSIX specifies; getdate_err's POSIX
 * values (1: DATEMSK unset, 2: can't open it, ...) don't really apply
 * here, so only 1 (no usable input) and 7 (no template matched) are
 * ever set.
 */
#include <time.h>
#include <string.h>
#include "libc.h"

int getdate_err;

static const char *const templates[] = {
	"%Y-%m-%d %H:%M:%S",
	"%Y-%m-%d %H:%M",
	"%Y-%m-%d",
	"%m/%d/%Y %H:%M:%S",
	"%m/%d/%Y",
	"%d %B %Y",
	"%d %b %Y",
	"%B %d, %Y",
	"%H:%M:%S",
	"%H:%M",
};

struct tm *getdate(const char *s)
{
	static struct tm tm;
	size_t i;

	if (!s || !*s) { getdate_err = 1; return NULL; }

	for (i = 0; i < sizeof templates / sizeof *templates; i++) {
		struct tm t;
		char *end;
		memset(&t, 0, sizeof t);
		t.tm_mday = 1;    /* so a time-only template doesn't ask mktime for day 0 */
		end = strptime(s, templates[i], &t);
		if (!end) continue;
		while (*end == ' ' || *end == '\t') end++;
		if (*end) continue;
		mktime(&t);       /* normalize, and fill tm_wday/tm_yday */
		tm = t;
		return &tm;
	}
	getdate_err = 7;
	return NULL;
}
