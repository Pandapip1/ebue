/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdlib.h>
#include <string.h>

int getsubopt(char **opt, char *const *keys, char **val)
{
	char *s = *opt;
	size_t i;

	*val = 0;
	*opt = strchr(s, ',');
	if (*opt) *(*opt)++ = 0;
	else *opt = s + strlen(s);

	for (i = 0; keys[i]; i++) {
		size_t l = strlen(keys[i]);
		if (strncmp(keys[i], s, l)) continue;
		if (s[l] == '=') *val = s + l + 1;
		else if (s[l]) continue;
		return (int)i;
	}
	*val = s;
	return -1;
}
