/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <strings.h>
#include <string.h>

void bcopy(const void *s1, void *s2, size_t n)
{
	memmove(s2, s1, n);
}
