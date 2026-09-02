/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hand-written binary-search lookups over the generated Unicode range/
 * case-mapping tables in src/internal/unicode_tables.c. This file is the
 * only thing here that is NOT generated: the data is mechanical (see
 * tools/gen-unicode-tables.py), but "binary-search a sorted range table"
 * and "binary-search a sorted pair table" are two small, stable
 * algorithms that do not change when the Unicode version backing the
 * data does, so they are written once, by hand, rather than re-emitted
 * on every regeneration.
 *
 * Declared in src/internal/libc.h (see that header's own "Unicode
 * Character Database" section for the contract each function has).
 */
#include <stddef.h>

#include "libc.h"
#include "unicode_tables.h"

static int in_ranges(const struct unicode_range *r, size_t n, unsigned cp)
{
	size_t lo = 0, hi = n;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (cp < r[mid].lo) hi = mid;
		else if (cp > r[mid].hi) lo = mid + 1;
		else return 1;
	}
	return 0;
}

static unsigned map_pair(const struct unicode_casepair *p, size_t n,
                          unsigned cp)
{
	size_t lo = 0, hi = n;

	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (cp < p[mid].from) hi = mid;
		else if (cp > p[mid].from) lo = mid + 1;
		else return p[mid].to;
	}
	return cp;
}

int __unicode_is_alpha(unsigned cp)
{
	return in_ranges(unicode_alpha_ranges, unicode_alpha_ranges_count, cp);
}

int __unicode_is_upper(unsigned cp)
{
	return in_ranges(unicode_upper_ranges, unicode_upper_ranges_count, cp);
}

int __unicode_is_lower(unsigned cp)
{
	return in_ranges(unicode_lower_ranges, unicode_lower_ranges_count, cp);
}

int __unicode_is_digit(unsigned cp)
{
	return in_ranges(unicode_digit_ranges, unicode_digit_ranges_count, cp);
}

int __unicode_is_space(unsigned cp)
{
	return in_ranges(unicode_space_ranges, unicode_space_ranges_count, cp);
}

int __unicode_is_cntrl(unsigned cp)
{
	return in_ranges(unicode_cntrl_ranges, unicode_cntrl_ranges_count, cp);
}

int __unicode_is_xdigit(unsigned cp)
{
	return in_ranges(unicode_xdigit_ranges, unicode_xdigit_ranges_count, cp);
}

int __unicode_is_blank(unsigned cp)
{
	return in_ranges(unicode_blank_ranges, unicode_blank_ranges_count, cp);
}

int __unicode_is_print(unsigned cp)
{
	/* Defined as a complement (see libc.h): anything at or above 0x10000
	 * would otherwise silently read as "printable" by falling through
	 * every notprint range untouched, which is wrong -- there is no
	 * valid BMP code point up there for it to mean, so exclude the whole
	 * domain explicitly rather than let the table miss decide. */
	if (cp > 0xffff) return 0;
	return !in_ranges(unicode_notprint_ranges, unicode_notprint_ranges_count, cp);
}

int __unicode_is_combining(unsigned cp)
{
	return in_ranges(unicode_combining_ranges, unicode_combining_ranges_count, cp);
}

int __unicode_is_wide(unsigned cp)
{
	return in_ranges(unicode_wide_ranges, unicode_wide_ranges_count, cp);
}

unsigned __unicode_to_upper(unsigned cp)
{
	return map_pair(unicode_toupper_pairs, unicode_toupper_pairs_count, cp);
}

unsigned __unicode_to_lower(unsigned cp)
{
	return map_pair(unicode_tolower_pairs, unicode_tolower_pairs_count, cp);
}
