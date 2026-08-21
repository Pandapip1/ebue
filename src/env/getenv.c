/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <stdlib.h>
#include <string.h>
#include "libc.h"

/* Windows environment names are case-insensitive, and a program asking
 * for "PATH" must find "Path", which is how Windows spells it. */
static int name_eq(const char *e, const char *name, size_t l)
{
	size_t i;
	for (i = 0; i < l; i++) {
		int a = (unsigned char)e[i], b = (unsigned char)name[i];
		if (!a) return 0;
		if (a >= 'a' && a <= 'z') a -= 32;
		if (b >= 'a' && b <= 'z') b -= 32;
		if (a != b) return 0;
	}
	return e[l] == '=';
}

char **__env_find(const char *name, size_t l)
{
	char **e;
	if (!__environ) return 0;
	for (e = __environ; *e; e++)
		if (name_eq(*e, name, l)) return e;
	return 0;
}

char *getenv(const char *name)
{
	size_t l = strcspn(name, "=");
	char **e;
	if (!l || name[l]) return 0;
	e = __env_find(name, l);
	return e ? *e + l + 1 : 0;
}

char *secure_getenv(const char *name) { return getenv(name); }
