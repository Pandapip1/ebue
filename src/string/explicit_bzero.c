/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <string.h>

void explicit_bzero(void *d, size_t n)
{
	volatile unsigned char *p = d;
	while (n) { *p++ = 0; n--; }
}
