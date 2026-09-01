/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wctype.h>
#include <ctype.h>

int iswprint(wint_t wc)
{
	return isprint((int)wc);
}

int iswprint_l(wint_t wc, locale_t loc)
{
	(void)loc;
	return iswprint(wc);
}
