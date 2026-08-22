/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "libc.h"


/* Strings put in the environment by setenv/putenv are ours to free; the
 * ones crt1 built are too, since all of them were malloc'd.  Strings
 * given to putenv belong to the caller and are tracked so that they are
 * never freed. */
static char **putenv_strings;
static size_t nputenv;

static int is_putenv(char *s)
{
	size_t i;
	for (i = 0; i < nputenv; i++) if (putenv_strings[i] == s) return 1;
	return 0;
}

static int env_count(void)
{
	int n = 0;
	if (__environ) while (__environ[n]) n++;
	return n;
}

int __putenv(char *s, size_t l, char *owned)
{
	char **e = __env_find(s, l);
	if (e) {
		if (!is_putenv(*e)) free(*e);
		*e = s;
	} else {
		int n = env_count();
		char **ne = realloc(__environ, sizeof(char *) * (n + 2));
		if (!ne) { free(owned); return -1; }
		ne[n] = s;
		ne[n+1] = 0;
		__environ = ne;
	}
	(void)owned;
	return 0;
}

int setenv(const char *name, const char *value, int overwrite)
{
	size_t l1, l2;
	char *s;
	if (!name || !(l1 = strcspn(name, "=")) || name[l1]) { errno = EINVAL; return -1; }
	if (!overwrite && getenv(name)) return 0;
	l2 = strlen(value);
	s = malloc(l1 + l2 + 2);
	if (!s) return -1;
	memcpy(s, name, l1);
	s[l1] = '=';
	memcpy(s + l1 + 1, value, l2 + 1);
	return __putenv(s, l1, s);
}

int putenv(char *s)
{
	size_t l = strcspn(s, "=");
	char **np;
	if (!l || !s[l]) return unsetenv(s);
	np = realloc(putenv_strings, sizeof(char *) * (nputenv + 1));
	if (!np) return -1;
	putenv_strings = np;
	putenv_strings[nputenv++] = s;
	return __putenv(s, l, 0);
}

int unsetenv(const char *name)
{
	size_t l;
	char **e;
	if (!name || !(l = strcspn(name, "=")) || name[l]) { errno = EINVAL; return -1; }
	while ((e = __env_find(name, l))) {
		char **p = e;
		if (!is_putenv(*e)) free(*e);
		do p[0] = p[1]; while (*p++);
	}
	return 0;
}

int clearenv(void)
{
	char **e;
	if (__environ) {
		for (e = __environ; *e; e++) if (!is_putenv(*e)) free(*e);
		__environ[0] = 0;
	}
	return 0;
}
