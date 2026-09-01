/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int gethostname(char *name, size_t len)
{
	const char *h = getenv("COMPUTERNAME");
	size_t n;
	if (!h) h = "localhost";
	n = strlen(h);
	if (n >= len) {
		if (len) memcpy(name, h, len);
		return 0;
	}
	memcpy(name, h, n + 1);
	return 0;
}

// NOLINTEND(misc-include-cleaner)
