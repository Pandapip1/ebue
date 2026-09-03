/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Backed by real Unicode data (src/internal/unicode_data.c, generated
 * from Unicode 15.0.0 by tools/gen-unicode-tables.py). is_print is the
 * complement of Cc+Cf+Cs+Co+Cn+Zl+Zp; is_combining is Mn+Me (0 columns,
 * rendered onto the previous glyph); is_wide is East_Asian_Width
 * Wide/Fullwidth (2 columns).
 *
 * NUL is checked before any table lookup: U+0000 is itself Cc, so
 * is_print() would call it non-printable (-1), but wcwidth.html wants
 * width 0 for it specifically.
 */
#include <wchar.h>
#include "libc.h"

int wcwidth(wchar_t wc)
{
	unsigned cp = wc;

	if (cp == 0) return 0;
	if (!__unicode_is_print(cp)) return -1;
	if (__unicode_is_combining(cp)) return 0;
	if (__unicode_is_wide(cp)) return 2;
	return 1;
}
