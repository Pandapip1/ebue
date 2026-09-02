/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Backed by the real Unicode White_Space property -- see wctype.h's
 * banner comment and tools/gen-unicode-tables.py's docstring. */
#include <wctype.h>
#include "libc.h"

int iswspace(wint_t wc)
{
	return __unicode_is_space(wc);
}
