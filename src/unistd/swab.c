/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * swab(): copy nbytes bytes, exchanging adjacent pairs.  Pure byte
 * shuffling, nothing NT-specific about it, which is presumably why it
 * ended up declared here with no definition to go with it.
 *
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/swab.html:
 * negative nbytes does nothing; odd nbytes copies and swaps nbytes-1
 * bytes and leaves the disposition of the last byte unspecified (this
 * copies it through unswapped, the least surprising of the allowed
 * choices).
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <unistd.h>

void swab(const void *__restrict src, void *__restrict dest, ssize_t nbytes) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	const unsigned char *s = src;
	unsigned char *d = dest;
	ssize_t i, pairs;

	if (nbytes <= 0) return;
	pairs = nbytes / 2;
	for (i = 0; i < pairs; i++) {
		d[2 * i] = s[2 * i + 1];
		d[2 * i + 1] = s[2 * i];
	}
	i *= 2;
	if (i < nbytes) d[i] = s[i];
}

// NOLINTEND(misc-include-cleaner)
