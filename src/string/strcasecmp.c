/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <strings.h>
#include <ctype.h>

int strcasecmp(const char *_l, const char *_r)
{
	const unsigned char *l = (void *)_l, *r = (void *)_r;
	for (; *l && *r && (*l == *r || tolower(*l) == tolower(*r)); l++, r++);
	return tolower(*l) - tolower(*r);
}

int strcasecmp_l(const char *l, const char *r, locale_t loc)
{
	(void)loc;
	return strcasecmp(l, r);
}
