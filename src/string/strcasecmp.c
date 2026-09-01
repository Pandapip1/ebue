/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <strings.h>
#include <ctype.h>

int strcasecmp(const char *_l withtok(null_terminated),
	const char *_r withtok(null_terminated))
{
	const unsigned char *l = (void *)_l, *r = (void *)_r;
	int lc, rc;
	for (; *l && *r && (*l == *r || tolower(*l) == tolower(*r)); l++, r++);
	lc = tolower(*l);
	rc = tolower(*r);
	if (lc < rc) return -1;
	return lc > rc;
}

int strcasecmp_l(const char *l withtok(null_terminated),
	const char *r withtok(null_terminated), locale_t loc)
{
	(void)loc;
	return strcasecmp(l, r);
}
