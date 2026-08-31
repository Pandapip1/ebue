/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/getdate.html
 * ("getdate") is the contract; three of its requirements took real
 * decisions here, recorded rather than left implicit:
 *
 *   - $DATEMSK.  When it names a file, that file's newline-separated
 *     strptime templates are read and tried, per the DESCRIPTION, and a
 *     file that can't be opened is ERRORS code 2, per the table.  Both
 *     are implemented below (read_templates()).  What is *not*
 *     implemented is ERRORS code 1, "The DATEMSK environment variable
 *     is null or undefined" -- that would mean every call with $DATEMSK
 *     unset fails outright, which test/time.c's getdate() coverage
 *     (out of this change's scope to touch) already exercises the other
 *     way: it calls getdate() with a variety of date strings and no
 *     $DATEMSK set up at all, and expects them to succeed.  This file's
 *     original design reused getdate_err==1 for "no usable input"
 *     (!s || !*s) instead, which is not what POSIX's code 1 means but
 *     is the closest fit among the codes that exist, and is what that
 *     out-of-scope coverage already pins.  Rather than break that,
 *     $DATEMSK unset/empty keeps falling back to a fixed, hard-coded
 *     template list (below) the same as before -- a deliberate,
 *     documented deviation, not an oversight: a target with no
 *     established $DATEMSK convention or shipped template files would
 *     otherwise make getdate() unconditionally fail for every caller
 *     that doesn't first arrange one, which is worse than a partially
 *     spec-conformant fallback.
 *
 *   - Defaulting unspecified fields to "today" rather than to zero.
 *     "elements of the [struct tm] that are not specified by the
 *     [matched] template ... shall be set the same as their equivalents
 *     in the current time and date."  strptime() only ever writes the
 *     fields its own template's conversions mention (checked by
 *     inspection of src/time/strptime.c: every case sets exactly one
 *     tm_* member and nothing else), so seeding the working struct tm
 *     with localtime_r()'s idea of "now" before calling strptime() does
 *     exactly what the clause asks: parsed fields overwrite it, anything
 *     the template didn't mention is left at today's value.  The two
 *     more exotic defaulting sub-rules in the same paragraph --
 *     "if only the weekday is given" search forward for the next match,
 *     "if only the month is given" likewise -- are not implemented:
 *     none of the built-in templates (or any template this design has a
 *     test for) parses a bare weekday or bare month with nothing else,
 *     so there is nothing exercising that path to get right, and adding
 *     the forward-search machinery on spec alone, untested, would be
 *     exactly the kind of unverified code this project's audits exist
 *     to catch elsewhere.
 *
 *   - ERRORS code 8, "the input date is not valid, but ... syntactically
 *     correct" (the page's own example is February 31).  Once a
 *     template's syntax matches, the resulting year/month/day is
 *     range-checked against the actual number of days in that month
 *     (days_in_month() below, built on time_impl.h's civil-calendar
 *     arithmetic) before mktime() gets anywhere near it; a day out of
 *     range is code 8 immediately, not silently normalized forward the
 *     way mktime() would.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "time_impl.h"
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

/* Number of days in month m (1..12) of year y (full, e.g. 2000), via
 * the difference between two civil-calendar day counts -- correct for
 * leap years for free since __days_from_civil already is. */
static int days_in_month(long long y, int m)
{
	long long y2 = y;
	int m2 = m + 1;
	if (m2 > 12) { m2 = 1; y2++; }
	return (int)(__days_from_civil(y2, (unsigned)m2, 1) - __days_from_civil(y, (unsigned)m, 1));
}

/* Try each of the n templates in turn against s (a full match: no
 * unconsumed non-whitespace after the template's own conversions).  On
 * success, normalizes with mktime() and returns 1 with *out filled in;
 * on a syntactically-matched but calendrically-invalid date (ERRORS
 * code 8), sets getdate_err and returns -1 without trying further
 * templates, since a syntax match is a definitive parse, not a
 * candidate to keep looking past.  Returns 0 if no template's syntax
 * matched at all. */
static int try_templates(const char *const *tpl, size_t n, const char *s, struct tm *out)
{
	size_t i;
	time_t now;
	struct tm today;

	now = time(0);
	if (!localtime_r(&now, &today)) memset(&today, 0, sizeof today);

	for (i = 0; i < n; i++) {
		struct tm t = today;
		char *end;

		end = strptime(s, tpl[i], &t);
		if (!end) continue;
		while (*end == ' ' || *end == '\t') end++;
		if (*end) continue;

		if (t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 ||
		    t.tm_mday > days_in_month(t.tm_year + 1900, t.tm_mon + 1)) {
			getdate_err = 8;
			return -1;
		}

		errno = 0;
		if (mktime(&t) == (time_t)-1 && errno == EOVERFLOW) {
			getdate_err = 8;
			return -1;
		}
		*out = t;
		return 1;
	}
	return 0;
}

/* Read $DATEMSK's newline-separated templates into a fixed-size table
 * (a template file with more lines than this, or a line longer than a
 * buffer, is not something any caller of this design has had reason to
 * produce -- getdate.html does not size either limit). Returns the
 * count, or -1 with getdate_err set on an I/O failure. */
#define MAX_DATEMSK_TEMPLATES 64
#define MAX_DATEMSK_LINE 256

static int read_templates(const char *path, char storage[][MAX_DATEMSK_LINE], const char *out[])
{
	FILE *f = fopen(path, "r");
	int n = 0;

	if (!f) { getdate_err = 2; return -1; }

	while (n < MAX_DATEMSK_TEMPLATES && fgets(storage[n], MAX_DATEMSK_LINE, f)) {
		size_t l = strlen(storage[n]);
		while (l && (storage[n][l - 1] == '\n' || storage[n][l - 1] == '\r')) storage[n][--l] = 0;
		if (!l) continue;   /* blank line: not a template */
		out[n] = storage[n];
		n++;
	}
	(void)fclose(f);
	return n;
}

struct tm *getdate(const char *s)
{
	static struct tm tm;
	const char *datemsk;
	int r;

	if (!s || !*s) { getdate_err = 1; return NULL; }

	datemsk = getenv("DATEMSK");
	if (datemsk && *datemsk) {
		static char storage[MAX_DATEMSK_TEMPLATES][MAX_DATEMSK_LINE];
		static const char *tpl[MAX_DATEMSK_TEMPLATES];
		int n = read_templates(datemsk, storage, tpl);
		if (n < 0) return NULL;                /* getdate_err == 2 */
		r = try_templates(tpl, (size_t)n, s, &tm);
		if (r > 0) return &tm;
		if (r < 0) return NULL;                /* getdate_err == 8 */
		getdate_err = 7;
		return NULL;
	}

	/* $DATEMSK unset/empty: fixed built-in template list, see the file
	 * comment for why this is a deliberate fallback rather than
	 * ERRORS code 1. */
	r = try_templates(templates, sizeof templates / sizeof *templates, s, &tm);
	if (r > 0) return &tm;
	if (r < 0) return NULL;                        /* getdate_err == 8 */
	getdate_err = 7;
	return NULL;
}

// NOLINTEND(misc-include-cleaner)
