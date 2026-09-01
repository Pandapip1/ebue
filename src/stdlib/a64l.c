/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const char digits[] = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

long a64l(const char *s)
{
	uint32_t x = 0;
	int i;
	const char *d;
	for (i = 0; i < 6 && s[i]; i++) {
		d = strchr(digits, s[i]);
		if (!d) break;
		x |= (uint32_t)(d - digits) << (6 * i);
	}
	return (long)(int32_t)x;
}

char *l64a(long v)
{
	static char buf[7];
	uint32_t x = (uint32_t)v;
	char *p = buf;
	while (x) { *p++ = digits[x & 63]; x /= 64; }
	*p = 0;
	return buf;
}
