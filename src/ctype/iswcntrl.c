/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Backed by real Unicode General_Category Cc -- a closed, small set (the
 * C0 controls, DEL, and the C1 controls, nothing else) -- see wctype.h's
 * banner comment and tools/gen-unicode-tables.py's docstring. */
#include <wctype.h>
#include "libc.h"

int iswcntrl(wint_t wc)
{
	return __unicode_is_cntrl(wc);
}
