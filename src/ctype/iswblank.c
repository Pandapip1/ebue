/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Backed by real Unicode General_Category Zs (space separators) union
 * U+0009 TAB -- see wctype.h's banner comment and
 * tools/gen-unicode-tables.py's docstring. */
#include <wctype.h>
#include "libc.h"

int iswblank(wint_t wc)
{
	return __unicode_is_blank(wc);
}
