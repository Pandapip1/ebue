/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Backed by the real Unicode Alphabetic property (src/internal/
 * unicode_data.c, generated from Unicode 15.0.0 -- see wctype.h's own
 * banner comment for why this no longer matches isalpha(), and
 * tools/gen-unicode-tables.py's docstring for exactly what "Alphabetic"
 * covers). wc above 0xffff (WEOF included) and a lone surrogate half
 * both simply miss the table and answer false, with no special-casing
 * needed -- see __unicode_is_alpha()'s own contract in libc.h. */
#include <wctype.h>
#include "libc.h"

int iswalpha(wint_t wc)
{
	return __unicode_is_alpha(wc);
}

int iswalpha_l(wint_t wc, locale_t loc)
{
	(void)loc;
	return iswalpha(wc);
}
