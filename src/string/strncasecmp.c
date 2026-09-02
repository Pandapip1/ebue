/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <strings.h>
#include <ctype.h>

int strncasecmp(const char *_l, const char *_r, size_t n)
{
	const unsigned char *l = (void *)_l, *r = (void *)_r;
	int lc, rc;
	if (!n) return 0;
	n--;
	while (*l && *r && n && (*l == *r || tolower(*l) == tolower(*r))) {
		l++;
		r++;
		n--;
	}
	lc = tolower(*l);
	rc = tolower(*r);
	if (lc < rc) return -1;
	return lc > rc;
}

int strncasecmp_l(const char *l, const char *r, size_t n, locale_t loc)
{
	(void)loc;
	return strncasecmp(l, r, n);
}
