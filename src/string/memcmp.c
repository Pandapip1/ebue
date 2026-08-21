/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>

int memcmp(const void *vl, const void *vr, size_t n)
{
	const unsigned char *l = vl, *r = vr;
	for (; n && *l == *r; n--, l++, r++);
	return n ? *l - *r : 0;
}
