/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A strptime covering the same specifiers strftime writes (%Y %y %m %d
 * %H %M %S %e %j %a %A %b %B %p %z %% plus %n/%t as whitespace), which
 * is what round-tripping strftime's own output needs.  Unrecognized
 * conversions and the locale %E/%O modifiers are not implemented.
 */
#include <time.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "time_impl.h"

static const char *skip_ws(const char *s)
{
	while (isspace((unsigned char)*s)) s++;
	return s;
}

/* Parse up to maxdigits decimal digits (after optional leading blanks),
 * the way strptime's numeric conversions do. */
static const char *read_num(const char *s, int maxdigits, long *out)
{
	int n = 0;
	long v = 0;
	int neg = 0;
	s = skip_ws(s);
	if (*s == '+' || *s == '-') { neg = (*s == '-'); s++; }
	if (!isdigit((unsigned char)*s)) return NULL;
	while (isdigit((unsigned char)*s) && n < maxdigits) { v = v * 10 + (*s++ - '0'); n++; }
	*out = neg ? -v : v;
	return s;
}

/* Match a name (case-insensitively) against one of the tables, longest
 * candidate first isn't necessary since none is a prefix of another
 * within the same table, but abbreviations ARE prefixes of the full
 * names, so try full names before abbreviations. */
static const char *match_name(const char *s, const char *const *full, const char *const *abbr, int n, int *idx)
{
	for (int i = 0; i < n; i++) {
		size_t len = strlen(full[i]);
		if (!strncasecmp(s, full[i], len)) { *idx = i; return s + len; }
	}
	for (int i = 0; i < n; i++) {
		size_t len = strlen(abbr[i]);
		if (!strncasecmp(s, abbr[i], len)) { *idx = i; return s + len; }
	}
	return NULL;
}

char *strptime(const char *restrict s, const char *restrict f, struct tm *restrict tm)
{
	long v;
	int idx;
	int pm = -1;   /* -1: no %p seen; 0: AM; 1: PM */

	for (; *f; f++) {
		if (*f != '%') {
			if (isspace((unsigned char)*f)) { s = skip_ws(s); continue; }
			if (*s != *f) return NULL;
			s++;
			continue;
		}
		f++;
		if (!*f) return NULL;
		switch (*f) {
		/* Widths follow musl/glibc: %Y 4, %j 3, %u/%w 1, everything else 2,
		 * so an unseparated "%Y%m%d" doesn't let %Y swallow later fields. */
		case 'Y': if (!(s = read_num(s, 4, &v))) return NULL; tm->tm_year = (int)(v - 1900); break;
		case 'y': if (!(s = read_num(s, 2, &v))) return NULL; tm->tm_year = (int)(v < 69 ? v + 100 : v); break;
		case 'm': if (!(s = read_num(s, 2, &v))) return NULL; tm->tm_mon = (int)v - 1; break;
		case 'd': case 'e': if (!(s = read_num(s, 2, &v))) return NULL; tm->tm_mday = (int)v; break;
		case 'H': if (!(s = read_num(s, 2, &v))) return NULL; tm->tm_hour = (int)v; break;
		case 'I': if (!(s = read_num(s, 2, &v))) return NULL; tm->tm_hour = (int)v; break;
		case 'M': if (!(s = read_num(s, 2, &v))) return NULL; tm->tm_min = (int)v; break;
		case 'S': if (!(s = read_num(s, 2, &v))) return NULL; tm->tm_sec = (int)v; break;
		case 'j': if (!(s = read_num(s, 3, &v))) return NULL; tm->tm_yday = (int)v - 1; break;
		case 'u': if (!(s = read_num(s, 1, &v))) return NULL; tm->tm_wday = (int)(v == 7 ? 0 : v); break;
		case 'w': if (!(s = read_num(s, 1, &v))) return NULL; tm->tm_wday = (int)v; break;
		case 'a': case 'A':
			if (!(s = match_name(s, __ntlibc_day_name, __ntlibc_day_name_abbr, 7, &idx))) return NULL;
			tm->tm_wday = idx;
			break;
		case 'b': case 'B': case 'h':
			if (!(s = match_name(s, __ntlibc_month_name, __ntlibc_month_name_abbr, 12, &idx))) return NULL;
			tm->tm_mon = idx;
			break;
		case 'p':
			if (!strncasecmp(s, "AM", 2)) { pm = 0; s += 2; }
			else if (!strncasecmp(s, "PM", 2)) { pm = 1; s += 2; }
			else return NULL;
			break;
		case 'z':
			s = skip_ws(s);
			if (*s == 'Z') { tm->__tm_gmtoff = 0; s++; }
			else if (*s == '+' || *s == '-') {
				int sign = *s == '-' ? -1 : 1;
				long h, mn = 0;
				s++;
				if (!(s = read_num(s, 2, &h))) return NULL;
				if (*s == ':') s++;
				if (isdigit((unsigned char)*s)) { if (!(s = read_num(s, 2, &mn))) return NULL; }
				tm->__tm_gmtoff = sign * (h * 3600 + mn * 60);
			} else return NULL;
			break;
		case 'Z':
			while (isalpha((unsigned char)*s)) s++;
			break;
		case 'n': case 't':
			s = skip_ws(s);
			break;
		case '%':
			if (*s != '%') return NULL;
			s++;
			break;
		default:
			return NULL;
		}
	}
	if (pm == 1 && tm->tm_hour < 12) tm->tm_hour += 12;
	else if (pm == 0 && tm->tm_hour == 12) tm->tm_hour = 0;
	return (char *)s;
}
