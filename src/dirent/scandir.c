/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * scandir, and the two comparators dirent.h advertises for it, built
 * entirely on opendir/readdir + qsort: nothing here talks to NT
 * directly.  Each surviving entry is copied into a malloc'd block sized
 * to its actual name, not a full struct dirent, the way musl does it.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

int scandir(const char *path, struct dirent ***res,
            int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **))
{
	DIR *dp;
	struct dirent *d, **list = 0;
	size_t n = 0, cap = 0;

	dp = opendir(path);
	if (!dp) return -1;

	errno = 0;
	while ((d = readdir(dp))) {
		struct dirent *copy;
		size_t namelen;

		if (filter && !filter(d)) continue;

		if (n == cap) {
			size_t newcap = cap ? cap * 2 : 16;
			struct dirent **nl = __malloc(newcap * sizeof *nl);
			if (!nl) goto fail;
			if (list) memcpy(nl, list, n * sizeof *nl);
			__free(list);
			list = nl;
			cap = newcap;
		}

		namelen = strlen(d->d_name);
		copy = __malloc(offsetof(struct dirent, d_name) + namelen + 1);
		if (!copy) goto fail;
		memcpy(copy, d, offsetof(struct dirent, d_name) + namelen + 1);
		list[n++] = copy;
	}
	if (errno) goto fail;
	closedir(dp);

	if (compar) qsort(list, n, sizeof *list, (int (*)(const void *, const void *))compar);
	*res = list;
	return (int)n;

fail:
	{
		size_t i;
		int e = errno ? errno : ENOMEM;
		for (i = 0; i < n; i++) __free(list[i]);
		__free(list);
		closedir(dp);
		errno = e;
		return -1;
	}
}

int alphasort(const struct dirent **a, const struct dirent **b)
{
	return strcmp((*a)->d_name, (*b)->d_name);
}

int versionsort(const struct dirent **a, const struct dirent **b)
{
	return strverscmp((*a)->d_name, (*b)->d_name);
}
