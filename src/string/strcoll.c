/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <locale.h>

int strcoll(const char *l, const char *r)
{
	return strcmp(l, r);
}

int strcoll_l(const char *l, const char *r, locale_t loc)
{
	(void)loc;
	return strcmp(l, r);
}
