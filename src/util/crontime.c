/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See src/util/crontime.h for the grammar this implements and why.
 */
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "crontime.h"

/* Matches `text` (of length `len`) case-insensitively against one of
 * the three-letter `names`, returning lo+index on a match or -1. */
static int match_name(const char *text, size_t len, int lo, int hi, const char *const *names)
{
	int i;

	if (!names || len != 3) return -1;
	for (i = lo; i <= hi; i++) {
		const char *n = names[i - lo];
		if (tolower((unsigned char)text[0]) == n[0] &&
		    tolower((unsigned char)text[1]) == n[1] &&
		    tolower((unsigned char)text[2]) == n[2])
			return i;
	}
	return -1;
}

/* Parses one number-or-name at `*pp`, advancing `*pp` past it. `dow7`
 * folds a literal 7 to 0 (day-of-week's own special case; harmless
 * for every other field since 7 is never in range there anyway
 * except month, where folding would be wrong -- so this is only ever
 * passed 1 for the dow field). Returns the value or -1 on a malformed
 * token. */
static int parse_one(const char **pp, int lo, int hi, const char *const *names, int dow7)
{
	const char *p = *pp;
	const char *start = p;
	long v;
	char *end;

	if (isalpha((unsigned char)*p)) {
		while (isalpha((unsigned char)*p)) p++;
		v = match_name(start, (size_t)(p - start), lo, hi, names);
		if (v < 0) return -1;
		*pp = p;
		return (int)v;
	}
	if (!isdigit((unsigned char)*p)) return -1;
	v = strtol(p, &end, 10);
	if (end == p) return -1;
	*pp = end;
	if (dow7 && v == 7) v = 0;
	if (v < lo || v > hi) return -1;
	return (int)v;
}

int __crontime_parse_field(const char *text, int lo, int hi,
	const char *const *names, unsigned char *out)
{
	const char *p = text;
	int dow7 = (lo == 0 && hi == 6);

	/* Every caller-supplied `out` array is sized to cover index 0
	 * through hi inclusive (struct crontime's own minute[60]/hour[24]/
	 * dom[32]/month[13]/dow[7] -- dom/month simply leave index 0
	 * unused), so zeroing [0,hi] is always safe and always enough. */
	memset(out, 0, (size_t)(hi + 1) * sizeof out[0]);

	if (!*p) return -1;

	for (;;) {
		int a, b, step = 1;

		if (*p == '*') {
			a = lo;
			b = hi;
			p++;
		} else {
			a = parse_one(&p, lo, hi, names, dow7);
			if (a < 0) return -1;
			if (*p == '-') {
				p++;
				b = parse_one(&p, lo, hi, names, dow7);
				if (b < 0) return -1;
			} else {
				b = a;
			}
		}
		if (*p == '/') {
			long s;
			char *end;
			p++;
			if (!isdigit((unsigned char)*p)) return -1;
			s = strtol(p, &end, 10);
			if (end == p || s <= 0) return -1;
			p = end;
			step = (int)s;
		}
		if (b < a) return -1; /* crontab(5) ranges never wrap */
		{
			int v;
			for (v = a; v <= b; v += step) out[v] = 1;
		}
		if (*p == ',') { p++; continue; }
		break;
	}
	return *p ? -1 : 0;
}

static const char *const month_names[12] = {
	"jan", "feb", "mar", "apr", "may", "jun",
	"jul", "aug", "sep", "oct", "nov", "dec"
};
static const char *const dow_names[7] = {
	"sun", "mon", "tue", "wed", "thu", "fri", "sat"
};

int __crontime_parse(const char *min, const char *hour, const char *dom,
	const char *mon, const char *dow, struct crontime *out)
{
	if (__crontime_parse_field(min, 0, 59, 0, out->minute) < 0) return -1;
	if (__crontime_parse_field(hour, 0, 23, 0, out->hour) < 0) return -1;
	if (__crontime_parse_field(dom, 1, 31, 0, out->dom) < 0) return -1;
	if (__crontime_parse_field(mon, 1, 12, month_names, out->month) < 0) return -1;
	if (__crontime_parse_field(dow, 0, 6, dow_names, out->dow) < 0) return -1;
	out->dom_is_star = (dom[0] == '*' && dom[1] == 0);
	return 0;
}

int __crontime_matches(const struct crontime *e, const struct tm *t)
{
	int mon1 = t->tm_mon + 1;

	if (!e->minute[t->tm_min] || !e->hour[t->tm_hour] || !e->month[mon1])
		return 0;
	/* crontab(1p): day-of-month and day-of-week are AND'd normally,
	 * but OR'd when day-of-month was literally "*" and day-of-week
	 * is a real restriction -- see struct crontime's own comment. */
	if (e->dom_is_star) return e->dow[t->tm_wday];
	return e->dom[t->tm_mday] && e->dow[t->tm_wday];
}
