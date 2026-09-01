/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>

void *bsearch(const void *key, const void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	const unsigned char *b = base;
	while (n) {
		size_t half = n / 2;
		const unsigned char *p = b + half * sz;
		int c = cmp(key, p);
		if (c == 0) return (void *)p;
		if (c > 0) { b = p + sz; n -= half + 1; }
		else n = half;
	}
	return 0;
}
