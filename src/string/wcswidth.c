/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wcswidth.html: "determine the number of column positions required for
 * n wide-character codes (or fewer than n wide-character codes if a null
 * wide-character code is encountered before n wide-character codes are
 * exhausted) in the string". RETURN VALUE: the total column count, or -1
 * "if any of the n wide-character codes in the string pointed to by
 * pwcs is not printable" -- checked via wcwidth() itself, per
 * character, stopping at the first -1 (a non-printable code point makes
 * the whole answer -1, not just that character's contribution).
 */
#include <wchar.h>

int wcswidth(const wchar_t *pwcs, size_t n)
{
	int total = 0, w;
	size_t i;

	for (i = 0; i < n && pwcs[i]; i++) {
		w = wcwidth(pwcs[i]);
		if (w < 0) return -1;
		total += w;
	}
	return total;
}
