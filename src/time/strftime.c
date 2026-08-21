/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Covers the conversion specifiers programs actually use in practice
 * (%Y %m %d %H %M %S %A %a %B %b %j %p %Z %z %% plus the common
 * composites %c %x %X %D %F %T %R %r and the odds and ends %C %e %I %n
 * %t %u %w %y).  Not implemented: the week-number family (%U %W %V %G
 * %g), %s, and the locale-alternate %E/%O modifiers -- this target has
 * only the "C" locale and no week-numbering rules were worth the extra
 * code for a from-scratch libc.  An unrecognized %<letter> is passed
 * through literally, which is what several other libcs do rather than
 * silently eating input.
 *
 * POSIX strftime returns 0 (buffer contents unspecified) if the result
 * including the NUL wouldn't fit in max bytes, rather than truncating
 * like snprintf; PUT_CH below detects that and bails to `done` with
 * overflow set.
 */
#include <time.h>
#include <string.h>
#include "time_impl.h"

static size_t do_strftime(char *restrict s, size_t max, const char *restrict f, const struct tm *restrict tm)
{
	size_t pos = 0;
	int overflow = 0;

#define PUT_CH(c) do { if (pos + 1 >= max) { overflow = 1; goto done; } s[pos++] = (char)(c); } while (0)
#define PUT_STR(str) do { const char *_s = (str); while (*_s) PUT_CH(*_s++); } while (0)
#define PUT_NUM(v, w, pad) do { \
		long _v = (long)(v); \
		char _tmp[24]; int _n; \
		if (_v < 0) { PUT_CH('-'); _v = -_v; } \
		_n = __num_digits(_tmp, (int)sizeof _tmp, (unsigned long)_v, (w), (pad)); \
		for (int _i = 0; _i < _n; _i++) PUT_CH(_tmp[_i]); \
	} while (0)

	for (; *f; f++) {
		int wday, mon;
		if (*f != '%') { PUT_CH(*f); continue; }
		f++;
		if (!*f) break;

		wday = (unsigned)tm->tm_wday < 7 ? tm->tm_wday : 0;
		mon = (unsigned)tm->tm_mon < 12 ? tm->tm_mon : 0;

		switch (*f) {
		case 'a': PUT_STR(__ntlibc_day_name_abbr[wday]); break;
		case 'A': PUT_STR(__ntlibc_day_name[wday]); break;
		case 'h': case 'b': PUT_STR(__ntlibc_month_name_abbr[mon]); break;
		case 'B': PUT_STR(__ntlibc_month_name[mon]); break;
		case 'c':
			PUT_STR(__ntlibc_day_name_abbr[wday]); PUT_CH(' ');
			PUT_STR(__ntlibc_month_name_abbr[mon]); PUT_CH(' ');
			PUT_NUM(tm->tm_mday, 2, ' '); PUT_CH(' ');
			PUT_NUM(tm->tm_hour, 2, '0'); PUT_CH(':');
			PUT_NUM(tm->tm_min, 2, '0'); PUT_CH(':');
			PUT_NUM(tm->tm_sec, 2, '0'); PUT_CH(' ');
			PUT_NUM(tm->tm_year + 1900, 4, '0');
			break;
		case 'C': PUT_NUM((tm->tm_year + 1900) / 100, 2, '0'); break;
		case 'd': PUT_NUM(tm->tm_mday, 2, '0'); break;
		case 'D':
			PUT_NUM(tm->tm_mon + 1, 2, '0'); PUT_CH('/');
			PUT_NUM(tm->tm_mday, 2, '0'); PUT_CH('/');
			PUT_NUM((tm->tm_year + 1900) % 100, 2, '0');
			break;
		case 'e': PUT_NUM(tm->tm_mday, 2, ' '); break;
		case 'F':
			PUT_NUM(tm->tm_year + 1900, 4, '0'); PUT_CH('-');
			PUT_NUM(tm->tm_mon + 1, 2, '0'); PUT_CH('-');
			PUT_NUM(tm->tm_mday, 2, '0');
			break;
		case 'H': PUT_NUM(tm->tm_hour, 2, '0'); break;
		case 'I': { int h = tm->tm_hour % 12; PUT_NUM(h ? h : 12, 2, '0'); break; }
		case 'j': PUT_NUM(tm->tm_yday + 1, 3, '0'); break;
		case 'm': PUT_NUM(tm->tm_mon + 1, 2, '0'); break;
		case 'M': PUT_NUM(tm->tm_min, 2, '0'); break;
		case 'n': PUT_CH('\n'); break;
		case 'p': PUT_STR(tm->tm_hour < 12 ? "AM" : "PM"); break;
		case 'r':
			{ int h = tm->tm_hour % 12; PUT_NUM(h ? h : 12, 2, '0'); }
			PUT_CH(':'); PUT_NUM(tm->tm_min, 2, '0');
			PUT_CH(':'); PUT_NUM(tm->tm_sec, 2, '0');
			PUT_CH(' '); PUT_STR(tm->tm_hour < 12 ? "AM" : "PM");
			break;
		case 'R': PUT_NUM(tm->tm_hour, 2, '0'); PUT_CH(':'); PUT_NUM(tm->tm_min, 2, '0'); break;
		case 'S': PUT_NUM(tm->tm_sec, 2, '0'); break;
		case 't': PUT_CH('\t'); break;
		case 'T':
			PUT_NUM(tm->tm_hour, 2, '0'); PUT_CH(':');
			PUT_NUM(tm->tm_min, 2, '0'); PUT_CH(':');
			PUT_NUM(tm->tm_sec, 2, '0');
			break;
		case 'u': PUT_NUM(wday ? wday : 7, 1, '0'); break;
		case 'w': PUT_NUM(wday, 1, '0'); break;
		case 'x':
			PUT_NUM(tm->tm_mon + 1, 2, '0'); PUT_CH('/');
			PUT_NUM(tm->tm_mday, 2, '0'); PUT_CH('/');
			PUT_NUM((tm->tm_year + 1900) % 100, 2, '0');
			break;
		case 'X':
			PUT_NUM(tm->tm_hour, 2, '0'); PUT_CH(':');
			PUT_NUM(tm->tm_min, 2, '0'); PUT_CH(':');
			PUT_NUM(tm->tm_sec, 2, '0');
			break;
		case 'y': PUT_NUM((tm->tm_year + 1900) % 100, 2, '0'); break;
		case 'Y': PUT_NUM(tm->tm_year + 1900, 4, '0'); break;
		case 'z': {
			long off = tm->__tm_gmtoff;
			PUT_CH(off < 0 ? '-' : '+');
			if (off < 0) off = -off;
			PUT_NUM(off / 3600, 2, '0');
			PUT_NUM(off % 3600 / 60, 2, '0');
			break;
		}
		case 'Z': PUT_STR(tm->__tm_zone ? tm->__tm_zone : "UTC"); break;
		case '%': PUT_CH('%'); break;
		default: PUT_CH('%'); PUT_CH(*f); break;
		}
	}
done:
	if (overflow) return 0;
	s[pos] = 0;
	return pos;

#undef PUT_CH
#undef PUT_STR
#undef PUT_NUM
}

size_t strftime(char *restrict s, size_t max, const char *restrict f, const struct tm *restrict tm)
{
	if (!max) return 0;
	return do_strftime(s, max, f, tm);
}

size_t strftime_l(char *restrict s, size_t max, const char *restrict f, const struct tm *restrict tm, locale_t loc)
{
	(void)loc;
	return strftime(s, max, f, tm);
}
