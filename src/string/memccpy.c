/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>

void *memccpy(void *__restrict dest, const void *__restrict src, int c, size_t n)
{
	unsigned char *d = dest;
	const unsigned char *s = src;
	c = (unsigned char)c;
	for (; n; n--) {
		if ((*d++ = *s++) == c) return d;
	}
	return 0;
}
