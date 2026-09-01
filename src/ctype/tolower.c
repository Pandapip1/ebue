/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <ctype.h>

int tolower(int c)
{
	if (isupper(c)) return c | 32;
	return c;
}

int tolower_l(int c, locale_t loc)
{
	(void)loc;
	return tolower(c);
}
