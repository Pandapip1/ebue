/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <stdint.h>
#include <features.h>

__wraps void *memmove(void *dest, const void *src, size_t n)
{
	unsigned char *d = dest;
	const unsigned char *s = src;

	if (d == s) return d;
	/* musl's overlap test: (uintptr_t)s-(uintptr_t)d-n and -2*n both
	 * rely on unsigned wraparound (C99 6.2.5p9) to work for every
	 * relative position of s and d, not just d < s.  Not UB, and not
	 * worth restating byte-range-overlap logic just to dodge a check
	 * that exists to police unmarked wraparound, not this one. */
	if ((uintptr_t)s - (uintptr_t)d - n <= -2*n) return memcpy(d, s, n);
	if (d < s) {
		for (; n; n--) *d++ = *s++;
	} else {
		while (n) n--, d[n] = s[n];
	}
	return dest;
}
