/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>

int memcmp(const void *vl withtok(readable_span(n)),
	const void *vr withtok(readable_span(n)), size_t n)
{
	const unsigned char *l = vl, *r = vr;
	while (n > 0 && *l == *r) {
		n--;
		l++;
		r++;
	}
	return n ? *l - *r : 0;
}
