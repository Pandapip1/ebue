/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wcsstr(): the wchar_t mirror of strstr() (src/string/strstr.c), per
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/wcsstr.html
 * DESCRIPTION/RETURN VALUE -- "locate the first occurrence in the
 * wide-character string pointed to by ws1 of the sequence of wide
 * characters ... in the string pointed to by ws2", returning a pointer
 * to it, a null pointer if absent, and ws1 when ws2 has zero length.
 *
 * The search is over wchar_t units, not code points, which is exactly
 * right for this target's UTF-16 wchar_t: a supplementary character is
 * a surrogate pair, and matching the pair as two consecutive units
 * matches the character and nothing else -- a lone surrogate half can
 * never be confused with any BMP unit, because the 0xd800-0xdfff range
 * is reserved and never appears in well-formed text on its own.  Same
 * reasoning the already-implemented wcschr()/wcsncmp() rely on.
 *
 * strstr.c's byte version is reused in shape rather than in code: it
 * calls strchr()/strncmp(), and the wide equivalents wcschr()/wcsncmp()
 * are right here, so this is the same naive-search-after-first-unit-hit
 * algorithm with the two calls swapped.
 */
#include <wchar.h>

wchar_t *wcsstr(const wchar_t *h, const wchar_t *n)
{
	size_t l;

	if (!n[0]) return (wchar_t *)h;
	h = wcschr(h, *n);
	if (!h || !n[1]) return (wchar_t *)h;
	l = wcslen(n);
	for (; *h; h++) {
		if (*h == *n && !wcsncmp(h + 1, n + 1, l - 1)) return (wchar_t *)h;
	}
	return 0;
}
