/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>

char *strstr(const char *h, const char *n)
{
	size_t l;
	if (!n[0]) return (char *)h;
	h = strchr(h, *n);
	if (!h || !n[1]) return (char *)h;
	l = strlen(n);
	for (; *h; h++) {
		if (*h == *n && !strncmp(h+1, n+1, l-1)) return (char *)h;
	}
	return 0;
}
