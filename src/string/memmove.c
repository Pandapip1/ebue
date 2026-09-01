/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <stdint.h>
#include <features.h>
#include "ownership_stubs.h"

__wraps void *memmove(void *dest withtok(writable_span(n)),
	const void *src withtok(readable_span(n)), size_t n) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	unsigned char *d = dest;
	const unsigned char *s = src;

	if (d == s) return d;
	/* musl's overlap test: (uintptr_t)s-(uintptr_t)d-n and -2*n both
	 * rely on unsigned wraparound (C99 6.2.5p9) to work for every
	 * relative position of s and d, not just d < s.  Not UB, and not
	 * worth restating byte-range-overlap logic just to dodge a check
	 * that exists to police unmarked wraparound, not this one. */
	if ((uintptr_t)s - (uintptr_t)d - n <= -2*n) {
		__ownership_writable_span(d, n);
		__ownership_readable_span(s, n);
		__ownership_disjoint_span(d, s, n);
		return memcpy(d, s, n);
	}
	/* Copy direction is a flat-address-space question, the same one the
	 * overlap test above already answers through uintptr_t rather than
	 * through relational pointer comparison: dest and src are two
	 * independent caller-supplied buffers in the general case, not
	 * necessarily one object, so `d < s` is exactly the comparison ISO C
	 * leaves undefined for pointers into unrelated objects (6.5.8p5).
	 * Every real target this ships to has one flat address space where
	 * that comparison is well-defined in practice, but there is no
	 * reason to rely on it a second time in the same function when the
	 * first relies on uintptr_t instead. */
	if ((uintptr_t)d < (uintptr_t)s) {
		for (; n; n--) *d++ = *s++;
	} else {
		while (n) n--, d[n] = s[n];
	}
	return dest;
}
