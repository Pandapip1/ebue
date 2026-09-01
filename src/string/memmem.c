/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- GNU feature-test macro has its specified reserved spelling
#include <string.h>

void *memmem(const void *h0, size_t k, const void *n0, size_t l)
{
	const unsigned char *h = h0, *n = n0;
	size_t i, last;

	if (!l) return (void *)h;
	if (k < l) return 0;
	if (l == 1) return memchr(h0, *n, k);
	last = k - l;
	for (i = 0; i <= last; i++) {
		if (h[i] == *n && !memcmp(h+i+1, n+1, l-1)) return (void *)(h+i);
	}
	return 0;
}
