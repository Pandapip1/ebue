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
#include <unistd.h>

void swab(const void *__restrict src, void *__restrict dest, ssize_t nbytes)
{
	const unsigned char *s = src;
	unsigned char *d = dest;
	ssize_t i;

	if (nbytes <= 0) return;
	for (i = 0; i + 1 < nbytes; i += 2) {
		d[i] = s[i + 1];
		d[i + 1] = s[i];
	}
	if (i < nbytes) d[i] = s[i];
}
