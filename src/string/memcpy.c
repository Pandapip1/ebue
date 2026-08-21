/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <stdint.h>

void *memcpy(void *__restrict dest, const void *__restrict src, size_t n)
{
	unsigned char *d = dest;
	const unsigned char *s = src;

	if (n >= 4*sizeof(size_t) && ((uintptr_t)d & (sizeof(size_t)-1)) == ((uintptr_t)s & (sizeof(size_t)-1))) {
		while ((uintptr_t)d & (sizeof(size_t)-1)) { *d++ = *s++; n--; }
		for (; n >= sizeof(size_t); n -= sizeof(size_t), d += sizeof(size_t), s += sizeof(size_t))
			*(size_t *)d = *(const size_t *)s;
	}
	for (; n; n--) *d++ = *s++;
	return dest;
}
