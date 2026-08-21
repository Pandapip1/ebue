/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include <string.h>

char *strrchr(const char *s, int c)
{
	return memrchr(s, c, strlen(s)+1);
}
