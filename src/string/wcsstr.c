/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Matches wchar_t units, not code points: a supplementary character is a
 * surrogate pair, matched as two consecutive units, and a lone surrogate
 * half never collides with a BMP unit since 0xd800-0xdfff never appears
 * standalone in well-formed text.
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
