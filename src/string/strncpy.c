/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>

char *strncpy(char *__restrict d, const char *__restrict s, size_t n)
{
	stpncpy(d, s, n);
	return d;
}
