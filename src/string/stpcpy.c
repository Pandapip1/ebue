/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>

char *stpcpy(char *__restrict d, const char *__restrict s)
{
	for (; (*d = *s); s++, d++);
	return d;
}
