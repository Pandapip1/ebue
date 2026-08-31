/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wcscasecmp()/wcsncasecmp() and their _l forms: the wchar_t mirrors
 * of strcasecmp()/strncasecmp() (src/string/strcasecmp.c,
 * strncasecmp.c), per
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/wcscasecmp.html
 * DESCRIPTION, RETURN VALUE -- compare "ignoring differences in case",
 * returning an integer greater than, equal to, or less than zero.
 *
 * Case folding goes through towlower() (src/ctype/towlower.c), the
 * same way the byte versions go through tolower().  include/wctype.h
 * states this tree's standing decision that classification and
 * conversion are ASCII-only in its single C/POSIX locale, so towlower()
 * is the identity outside 'A'-'Z'; that makes the fold a no-op for
 * every code point from U+0080 up, which is the documented,
 * deliberate behaviour of the whole wctype family here and not a gap
 * specific to this file.
 *
 * A lone surrogate half folds to itself for the same reason, so
 * comparing UTF-16 text unit by unit is well defined: the surrogate
 * range is never equal to any BMP unit, and neither half is ever
 * case-mapped into or out of that range.
 *
 * The _l forms ignore their locale_t exactly as strcasecmp_l() and
 * strncasecmp_l() do -- src/misc/locale.c never produces a locale
 * other than C/POSIX, so there is no second behaviour to select.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <wchar.h>
#include <wctype.h>
#include <locale.h>

int wcscasecmp(const wchar_t *l, const wchar_t *r)
{
	for (; *l && *r && (*l == *r || towlower(*l) == towlower(*r)); l++, r++);
	return (int)towlower(*l) - (int)towlower(*r);
}

int wcsncasecmp(const wchar_t *l, const wchar_t *r, size_t n)
{
	if (!n) return 0;
	n--;
	for (; *l && *r && n && (*l == *r || towlower(*l) == towlower(*r)); l++, r++, n--);
	return (int)towlower(*l) - (int)towlower(*r);
}

int wcscasecmp_l(const wchar_t *l, const wchar_t *r, locale_t loc)
{
	(void)loc;
	return wcscasecmp(l, r);
}

int wcsncasecmp_l(const wchar_t *l, const wchar_t *r, size_t n, locale_t loc)
{
	(void)loc;
	return wcsncasecmp(l, r, n);
}

// NOLINTEND(misc-include-cleaner)
