/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wcwidth.html RETURN VALUE: 0 for the null wide-character code, -1 if
 * wc "does not correspond to a printable wide-character code", otherwise
 * the real terminal column count (1 for an ordinary character, 2 for a
 * wide/fullwidth one).
 *
 * Backed by real Unicode data (src/internal/unicode_data.c, generated
 * from Unicode 15.0.0 by tools/gen-unicode-tables.py -- see that
 * script's docstring for exactly what each property below means):
 *
 *   - __unicode_is_print(): the complement of Cc+Cf+Cs+Co+Cn+Zl+Zp.
 *     This alone already answers -1 for every control character (Cc,
 *     which is where the C0/C1 controls and DEL live) and for a lone
 *     surrogate half (Cs) -- see the wc == 0 special case below for the
 *     one printable-per-Unicode exception this function does NOT take
 *     that answer from.
 *   - __unicode_is_combining(): Mn+Me, the real "combining mark" set --
 *     0 columns, because a terminal renders these composed onto the
 *     previous glyph rather than advancing the cursor.
 *   - __unicode_is_wide(): East_Asian_Width Wide or Fullwidth -- 2
 *     columns, real CJK-width accounting.
 *   - anything else printable and non-combining: the ordinary 1 column.
 *
 * NUL is handled before any table lookup because U+0000 is itself
 * General_Category Cc (it is one of the C0 controls, part of the very
 * range __unicode_is_print() answers false for) -- wcwidth.html
 * carves out exactly this one code point as width 0 rather than -1,
 * so it needs its own check up front, not a table entry.
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
