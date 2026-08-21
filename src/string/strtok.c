/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <string.h>

char *strtok(char *__restrict s, const char *__restrict sep)
{
	static char *p;
	if (!s && !(s = p)) return 0;
	s += strspn(s, sep);
	if (!*s) return p = 0;
	p = s + strcspn(s, sep);
	if (*p) *p++ = 0;
	else p = 0;
	return s;
}
