/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Composed, not tabled -- the same style src/ctype/isalnum.c already
 * uses (isalpha() || isdigit(), no table of its own): a visible glyph
 * is anything printable that is not white space. */
#include <wctype.h>

int iswgraph(wint_t wc)
{
	return iswprint(wc) && !iswspace(wc);
}
