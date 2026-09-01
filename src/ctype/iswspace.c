/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wctype.h>
#include <ctype.h>

int iswspace(wint_t wc)
{
	return isspace((int)wc);
}

int iswspace_l(wint_t wc, locale_t loc)
{
	(void)loc;
	return iswspace(wc);
}
