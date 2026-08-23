/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <wctype.h>
#include <ctype.h>

int iswspace(wint_t wc)
{
	return isspace((int)wc);
}
