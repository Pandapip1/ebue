/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <stdint.h>

#define ALIGN (sizeof(size_t))
#define ONES ((size_t)-1/UCHAR_MAX_)
#define UCHAR_MAX_ 255
#define HIGHS (ONES * (UCHAR_MAX_/2+1))
#define HASZERO(x) (((x)-ONES) & ~(x) & HIGHS)

size_t strlen(const char *s)
{
	const char *a = s;
	const size_t *w;
	for (; (uintptr_t)s % ALIGN; s++) if (!*s) return s-a;
	for (w = (const void *)s; !HASZERO(*w); w++);
	s = (const void *)w;
	for (; *s; s++);
	return s-a;
}
