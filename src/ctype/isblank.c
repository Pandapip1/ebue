/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <ctype.h>

int isblank(int c)
{
	return c == ' ' || c == '\t';
}

int isblank_l(int c, locale_t loc)
{
	(void)loc;
	return isblank(c);
}
