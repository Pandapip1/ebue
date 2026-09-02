/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Backed by the real Unicode Lowercase property -- see wctype.h's banner
 * comment and tools/gen-unicode-tables.py's docstring. */
#include <wctype.h>
#include "libc.h"

int iswlower(wint_t wc)
{
	return __unicode_is_lower(wc);
}

int iswlower_l(wint_t wc, locale_t loc)
{
	(void)loc;
	return iswlower(wc);
}
