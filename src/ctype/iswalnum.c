/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Composed, not tabled -- same shape as src/ctype/isalnum.c. */
#include <wctype.h>

int iswalnum(wint_t wc)
{
	return iswalpha(wc) || iswdigit(wc);
}

int iswalnum_l(wint_t wc, locale_t loc)
{
	(void)loc;
	return iswalnum(wc);
}
