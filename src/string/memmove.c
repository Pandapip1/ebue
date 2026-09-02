/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <stdint.h>
#include <features.h>
#include "ownership_stubs.h"

/* Whether the n-byte spans [a, a+n) and [b, b+n) do NOT overlap. */
__wraps static int spans_disjoint(const void *a, const void *b, size_t n)
{
	uintptr_t distance = (uintptr_t)b - (uintptr_t)a;
	return distance - n <= -2*n;
}

__wraps void *memmove(void *dest withtok(writable_span(n)),
	const void *src withtok(readable_span(n)), size_t n) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	unsigned char *d = dest;
	const unsigned char *s = src;

	if (d == s) return d;
	if (spans_disjoint(d, s, n)) {
		__ownership_disjoint_span(d, s, n);
		return memcpy(d, s, n);
	}
	if ((uintptr_t)d < (uintptr_t)s) {
		while (n > 0) {
			*d = *s;
			d++;
			s++;
			n--;
		}
	} else {
		while (n) {
			n--;
			d[n] = s[n];
		}
	}
	return dest;
}
