/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <limits.h>

void *bsearch(const void *key, const void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *)) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	const unsigned char *b = base;
	size_t steps;

	/* Halving a size_t-sized interval reaches zero within its bit width. */
	for (steps = sizeof n * CHAR_BIT; steps-- > 0; ) {
		size_t half;
		const unsigned char *p;
		int c;
		if (!n) break;
		half = n / 2;
		p = b + half * sz;
		c = cmp(key, p);
		if (c == 0) return (void *)p;
		if (c > 0) { b = p + sz; n -= half + 1; }
		else n = half;
	}
	return 0;
}
