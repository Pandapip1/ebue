/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * iswctype.html DESCRIPTION/RETURN VALUE: "shall determine whether the
 * wide-character code wc has the character class charclass ...
 * returning true or false", and "if the value of charclass is 0 ...
 * these functions shall return 0."  The case labels below are the
 * wctype_t encoding wctype.c hands out, in the same order. */
#include <wctype.h>

int iswctype(wint_t wc, wctype_t desc) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	switch (desc) {
	case 1: return iswalnum(wc);
	case 2: return iswalpha(wc);
	case 3: return iswblank(wc);
	case 4: return iswcntrl(wc);
	case 5: return iswdigit(wc);
	case 6: return iswgraph(wc);
	case 7: return iswlower(wc);
	case 8: return iswprint(wc);
	case 9: return iswpunct(wc);
	case 10: return iswspace(wc);
	case 11: return iswupper(wc);
	case 12: return iswxdigit(wc);
	default: return 0;
	}
}

int iswctype_l(wint_t wc, wctype_t desc, locale_t loc)
{
	(void)loc;
	return iswctype(wc, desc);
}
