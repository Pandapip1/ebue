/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* qsort and qsort_r as an in-place heapsort: O(n log n) worst case, no
 * recursion, no allocation, and element size is arbitrary (elements are
 * swapped byte by byte, or by machine words when aligned). */
#include <stdlib.h>
#include <stdint.h>

typedef int (*cmp_r)(const void *, const void *, void *);

static void swap(unsigned char *a, unsigned char *b, size_t n)
{
	if (n % sizeof(size_t) == 0 && ((uintptr_t)a | (uintptr_t)b) % sizeof(size_t) == 0) {
		size_t *x = (size_t *)a, *y = (size_t *)b, t;
		for (n /= sizeof(size_t); n; n--, x++, y++) { t = *x; *x = *y; *y = t; }
	} else {
		unsigned char t;
		for (; n; n--, a++, b++) { t = *a; *a = *b; *b = t; }
	}
}

/* Sift base[i] down in a max-heap of n elements. */
static void sift(unsigned char *base, size_t n, size_t i, size_t sz, cmp_r cmp, void *arg)
{
	size_t c;
	while ((c = 2 * i + 1) < n) {
		if (c + 1 < n && cmp(base + c * sz, base + (c + 1) * sz, arg) < 0) c++;
		if (cmp(base + i * sz, base + c * sz, arg) >= 0) return;
		swap(base + i * sz, base + c * sz, sz);
		i = c;
	}
}

void qsort_r(void *b, size_t n, size_t sz, cmp_r cmp, void *arg)
{
	unsigned char *base = b;
	size_t i;

	if (n < 2 || !sz) return;
	for (i = n / 2; i > 0;) { i--; sift(base, n, i, sz, cmp, arg); }
	for (i = n - 1; i > 0; i--) {
		swap(base, base + i * sz, sz);
		sift(base, i, 0, sz, cmp, arg);
	}
}

static int wrap(const void *a, const void *b, void *f)
{
	return ((int (*)(const void *, const void *))f)(a, b);
}

void qsort(void *b, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
	qsort_r(b, n, sz, wrap, (void *)cmp);
}
