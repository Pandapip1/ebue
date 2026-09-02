/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

int gethostname(char *name withtok(writable_span(len)), size_t len)
{
	const char *h = getenv("COMPUTERNAME");
	size_t n;
	if (!h) h = "localhost";
	n = strlen(h);
	if (n >= len) {
		if (len) memmove(name, h, len);
		return 0;
	}
	if (snprintf(name, len, "%s", h) != (int)n) return -1;
	return 0;
}

// NOLINTEND(misc-include-cleaner)
