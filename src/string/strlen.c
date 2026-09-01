/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <stdint.h>
#include <features.h>

#define ALIGN (sizeof(size_t))
#define ONES ((size_t)-1/UCHAR_MAX_)
#define UCHAR_MAX_ 255
#define HIGHS (ONES * (UCHAR_MAX_/2+1))
/* (x)-ONES deliberately wraps in every word with a byte below 0x01 (in
 * particular the zero byte this is looking for): that wraparound is
 * what makes the classic SWAR has-a-zero-byte trick work in the first
 * place, not a bug in it. */
#define HASZERO(x) (((x)-ONES) & ~(x) & HIGHS)

__wraps size_t strlen(const char *s)
{
	const char *a = s;
	const size_t *w;
	/* Exact byte prefix before the next ALIGN boundary. */
	size_t align_bytes = (ALIGN - (uintptr_t)s % ALIGN) % ALIGN;
	for (; align_bytes > 0; s++, align_bytes--) if (!*s) return s-a;
	for (w = (const void *)s; !HASZERO(*w); w++);
	s = (const void *)w;
	for (; *s; s++);
	return s-a;
}
