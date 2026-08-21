/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <strings.h>
#include <string.h>

char *rindex(const char *s, int c)
{
	return strrchr(s, c);
}
