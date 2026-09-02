/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Backed by real Unicode General_Category Nd ("decimal digit", every
 * script's own contiguous 0-9 block, not just ASCII) -- see wctype.h's
 * banner comment and tools/gen-unicode-tables.py's docstring. */
#include <wctype.h>
#include "libc.h"

int iswdigit(wint_t wc)
{
	return __unicode_is_digit(wc);
}
