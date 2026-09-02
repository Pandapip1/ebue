/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Composed, not tabled -- same shape as src/ctype/ispunct.c: a
 * punctuation character is any visible glyph that isn't itself
 * alphanumeric. */
#include <wctype.h>

int iswpunct(wint_t wc)
{
	return iswgraph(wc) && !iswalnum(wc);
}

int iswpunct_l(wint_t wc, locale_t loc)
{
	(void)loc;
	return iswpunct(wc);
}
