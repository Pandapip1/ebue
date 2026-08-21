/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>
#include <stdlib.h>

char *strndup(const char *s, size_t n)
{
	size_t l = strnlen(s, n);
	char *d = malloc(l+1);
	if (!d) return 0;
	memcpy(d, s, l);
	d[l] = 0;
	return d;
}
