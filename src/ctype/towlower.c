/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * towlower.html: "If ... represents an uppercase wide-character code,
 * and there exists a corresponding lowercase wide-character code ...
 * the result shall be the corresponding lowercase wide-character code.
 * All other arguments in the domain are returned unchanged" -- which
 * includes a lone surrogate half and WEOF, neither of which iswupper()
 * ever answers true for.  Mirrors src/ctype/tolower.c's ASCII bit
 * trick, valid here because iswupper() already confined wc to 'A'-'Z'. */
#include <wctype.h>

wint_t towlower(wint_t wc)
{
	if (iswupper(wc)) return wc | 32;
	return wc;
}
