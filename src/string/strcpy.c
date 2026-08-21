/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>

char *strcpy(char *__restrict dest, const char *__restrict src)
{
	stpcpy(dest, src);
	return dest;
}
