/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>

char *stpncpy(char *__restrict d, const char *__restrict s, size_t n)
{
	for (; n && (*d = *s); n--, s++, d++);
	memset(d, 0, n);
	return d;
}
