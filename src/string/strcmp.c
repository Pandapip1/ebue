/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>

int strcmp(const char *l withtok(null_terminated),
	const char *r withtok(null_terminated))
{
	while (*l == *r && *l) {
		l++;
		r++;
	}
	return *(unsigned char *)l - *(unsigned char *)r;
}
