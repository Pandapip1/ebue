/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wctype.h>
#include <ctype.h>

int iswcntrl(wint_t wc)
{
	return iscntrl((int)wc);
}

int iswcntrl_l(wint_t wc, locale_t loc)
{
	(void)loc;
	return iswcntrl(wc);
}
