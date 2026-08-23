/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * towupper.html: same shape as towlower.html, the other direction.
 * Mirrors src/ctype/toupper.c's ASCII bit trick, valid here because
 * iswlower() already confined wc to 'a'-'z'. */
#include <wctype.h>

wint_t towupper(wint_t wc)
{
	if (iswlower(wc)) return wc & 0x5f;
	return wc;
}
