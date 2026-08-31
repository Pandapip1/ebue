/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* qsort and qsort_r as an in-place heapsort: O(n log n) worst case, no
 * recursion, no allocation, and element size is arbitrary (elements are
 * swapped byte by byte, or by machine words when aligned). */
#include <stdlib.h>
#include <stdint.h>

typedef int (*cmp_r)(const void *, const void *, void *);

/* a/b are required: both branches dereference through a/b unconditionally
 * whenever n > 0 (the aligned branch via `*x`/`*y`, the byte branch via
 * `*a`/`*b`), with no NULL check in either -- the checker's own report
 * flags both (one per branch, since they are syntactically separate, not
 * masked). Every real call site (sift()'s and qsort_r()'s own) computes
 * a/b as `base + k*sz` from qsort_r()'s own b, which is only ever
 * reached past the `if (n < 2 || !sz) return;` guard that already
 * excludes the one documented NULL-base case (n < 2); n itself is sz,
 * qsort_r()'s own element size, already proven nonzero by that same
 * guard.
 *
 * Marking a/b lets the checker explore further into the aligned
 * branch's own loop than before, now also flagging `*x`/`*y` (`size_t
 * *x = (size_t *)a, *y = (size_t *)b;`, then `x++, y++` each
 * iteration): a cast of an already-nonnull pointer, then advanced by
 * pointer arithmetic bounded by the same `n` -- sound by construction,
 * just past what this checker's nonnull propagation currently follows
 * across a loop increment. */
static void swap(unsigned char *a, unsigned char *b, size_t n) __attribute__((nonnull(1, 2)));
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

static int wrap(const void *a, const void *b, void *f) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	return ((int (*)(const void *, const void *))f)(a, b);
}

void qsort(void *b, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
	qsort_r(b, n, sz, wrap, (void *)cmp);
}
