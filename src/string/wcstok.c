/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wcstok(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/wcstok.html
 * DESCRIPTION, RETURN VALUE.
 *
 * NOTE FOR ANYONE TEMPTED TO "FIX" THE SIGNATURE: wcstok() takes a
 * third `wchar_t **restrict ptr` argument and is therefore the mirror
 * of strtok_r() (src/string/strtok_r.c), NOT of strtok().  That is not
 * an ntlibc extension -- it is wcstok's POSIX/C99 SYNOPSIS.  There is
 * no non-reentrant wcstok in any standard, so this function keeps no
 * static state at all and needs none.
 *
 * Body is strtok_r.c with wcsspn()/wcscspn() in place of strspn()/
 * strcspn(); the separator set is compared unit by unit, same
 * granularity as those two.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <wchar.h>

wchar_t *wcstok(wchar_t *__restrict s, const wchar_t *__restrict sep, wchar_t **__restrict p)
{
	if (!s) {
		s = *p;
		if (!s) return 0;
	}
	s += wcsspn(s, sep);
	if (!*s) return *p = 0;
	*p = s + wcscspn(s, sep);
	if (**p) *(*p)++ = 0;
	else *p = 0;
	return s;
}

// NOLINTEND(misc-include-cleaner)
