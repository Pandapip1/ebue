/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * towlower.html: "If ... represents an uppercase wide-character code,
 * and there exists a corresponding lowercase wide-character code ...
 * the result shall be the corresponding lowercase wide-character code.
 * All other arguments in the domain are returned unchanged."
 *
 * Backed by the real Unicode Simple_Lowercase_Mapping field
 * (src/internal/unicode_data.c, generated from Unicode 15.0.0) rather
 * than the ASCII bit trick src/ctype/tolower.c uses -- see wctype.h's
 * banner comment for why the two families diverge now, and
 * __unicode_to_lower()'s own contract in libc.h for why "no mapping
 * exists" (a lone surrogate half, WEOF, an already-lowercase or
 * uncased code point, or one like sharp s 'ß' with no *simple*
 * uppercase/lowercase counterpart at all) already returns wc unchanged
 * with no separate check needed here. */
#include <wctype.h>
#include "libc.h"

wint_t towlower(wint_t wc)
{
	return __unicode_to_lower(wc);
}

wint_t towlower_l(wint_t wc, locale_t loc)
{
	(void)loc;
	return towlower(wc);
}
