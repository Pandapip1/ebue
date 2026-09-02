/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * towupper.html: same shape as towlower.html, the other direction.
 * Backed by the real Unicode Simple_Uppercase_Mapping field -- see
 * towlower.c's own comment for the full reasoning, which mirrors
 * exactly here. */
#include <wctype.h>
#include "libc.h"

wint_t towupper(wint_t wc)
{
	return __unicode_to_upper(wc);
}
