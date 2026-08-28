/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef __SIZE_TYPE__ size_t;
size_t strlen(const char *);
int strcmp(const char *, const char *);

size_t literals(void)
{
	char local[] = "terminated";
	return strlen("literal") + strlen(local) + (size_t)strcmp(local, "literal");
}
