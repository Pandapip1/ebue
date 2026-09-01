/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wctype.h>
#include <ctype.h>

int iswblank(wint_t wc)
{
	return isblank((int)wc);
}

int iswblank_l(wint_t wc, locale_t loc)
{
	(void)loc;
	return iswblank(wc);
}
