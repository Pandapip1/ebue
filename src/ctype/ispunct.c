/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <ctype.h>

int ispunct(int c)
{
	return isgraph(c) && !isalnum(c);
}

int ispunct_l(int c, locale_t loc)
{
	(void)loc;
	return ispunct(c);
}
