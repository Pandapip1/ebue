/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wctype.h>
#include <ctype.h>

int iswpunct(wint_t wc)
{
	return ispunct((int)wc);
}

int iswpunct_l(wint_t wc, locale_t loc)
{
	(void)loc;
	return iswpunct(wc);
}
