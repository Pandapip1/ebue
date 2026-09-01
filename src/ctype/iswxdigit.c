/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wctype.h>
#include <ctype.h>

int iswxdigit(wint_t wc)
{
	return isxdigit((int)wc);
}

int iswxdigit_l(wint_t wc, locale_t loc)
{
	(void)loc;
	return iswxdigit(wc);
}
