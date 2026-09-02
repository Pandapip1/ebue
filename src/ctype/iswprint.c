/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Backed by the complement of General_Category Cc+Cf+Cs+Co+Cn
 * (unassigned)+Zl+Zp -- "has a real glyph or is at least a normal
 * space" -- see wctype.h's banner comment and
 * tools/gen-unicode-tables.py's docstring. */
#include <wctype.h>
#include "libc.h"

int iswprint(wint_t wc)
{
	return __unicode_is_print(wc);
}

int iswprint_l(wint_t wc, locale_t loc)
{
	(void)loc;
	return iswprint(wc);
}
