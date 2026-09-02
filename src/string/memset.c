/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <stdint.h>

void *memset(void *dest withtok(writable_span(n)), int c, size_t n) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	unsigned char *s = dest;
	size_t k;

	if (n >= 4*sizeof(size_t)) {
		size_t align_bytes = (sizeof(size_t) -
			((uintptr_t)s & (sizeof(size_t)-1))) & (sizeof(size_t)-1);
		size_t w = (unsigned char)c;
		w |= w << 8; w |= w << 16;
		if (sizeof(size_t) > 4) w |= w << 16 << 16;
		for (size_t i = 0; i < align_bytes; i++) s[i] = (unsigned char)c;
		s += align_bytes;
		n -= align_bytes;
		for (; n >= sizeof(size_t);
		     n -= sizeof(size_t), s += sizeof(size_t))
			*(size_t *)s = w;
	}
	for (k = 0; k < n; k++) s[k] = (unsigned char)c;
	return dest;
}
