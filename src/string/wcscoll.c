/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wcscoll()/wcscoll_l(): the wchar_t mirrors of strcoll()/strcoll_l()
 * (src/string/strcoll.c), per
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/wcscoll.html
 * DESCRIPTION, RETURN VALUE -- compare two wide-character strings
 * "using the collating sequence of the current locale", returning an
 * integer greater than, equal to, or less than zero.
 *
 * ntlibc has exactly one locale: src/misc/locale.c never accepts any
 * name other than "C"/"POSIX"/"".  strcoll.html's APPLICATION USAGE
 * says collation order in the POSIX locale is byte order, and the wide
 * analogue is code-unit order, so the collating sequence here IS
 * wcscmp()'s ordering and this is deliberately a one-line forward --
 * exactly the shape strcoll.c already has, and not a placeholder
 * awaiting a real collation table.  If this tree ever grows locales,
 * both files change together.
 *
 * wcscoll.html gives no ERRORS beyond an optional [EINVAL] for a string
 * containing characters outside the locale's domain.  Nothing here can
 * produce that: every wchar_t value, lone surrogate halves included, is
 * comparable as a code unit, so errno is never touched -- which also
 * means a caller can use the "set errno to 0, call, check errno"
 * idiom wcscoll.html describes and will correctly see no error.
 *
 * The _l form ignores its locale_t for the same reason strcoll_l()
 * does: there is no second locale to select.
 */
#include <wchar.h>
#include <locale.h>

int wcscoll(const wchar_t *l, const wchar_t *r)
{
	return wcscmp(l, r);
}

int wcscoll_l(const wchar_t *l, const wchar_t *r, locale_t loc)
{
	(void)loc;
	return wcscmp(l, r);
}
