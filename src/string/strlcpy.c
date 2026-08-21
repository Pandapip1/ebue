/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <string.h>

size_t strlcpy(char *d, const char *s, size_t n)
{
	size_t l = strlen(s);
	if (n) {
		size_t c = l < n-1 ? l : n-1;
		memcpy(d, s, c);
		d[c] = 0;
	}
	return l;
}
