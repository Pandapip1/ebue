/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Backed by the real Unicode Hex_Digit property (ASCII 0-9/A-F/a-f plus
 * their fullwidth U+FF10-FF46 forms) -- see wctype.h's banner comment
 * and tools/gen-unicode-tables.py's docstring. */
#include <wctype.h>
#include "libc.h"

int iswxdigit(wint_t wc)
{
	return __unicode_is_xdigit(wc);
}
