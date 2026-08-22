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

/* qsort_r's comparator has type int (*)(const void *, const void *, void *);
 * the caller's compar has type
 * int (*)(const struct dirent **, const struct dirent **).  Casting compar
 * itself to the qsort_r shape and calling it that way is undefined (C99
 * 6.3.2.3p8): the call would go through a function pointer type that does
 * not match how the function was defined, and -fsanitize=function traps on
 * exactly that.  This adapter instead has the type qsort_r actually calls,
 * and does the reinterpretation on the *arguments* -- an ordinary object
 * pointer conversion, not a function-pointer one -- before calling compar
 * through its own, correct type. */
static int scandir_cmp(const void *a, const void *b, void *arg)
{
	int (*compar)(const struct dirent **, const struct dirent **) = arg;
	return compar((const struct dirent **)a, (const struct dirent **)b);
}

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
			struct dirent **nl = __malloc(newcap * sizeof *nl); // NOLINT(bugprone-sizeof-expression) -- nl is dirent**, *nl is dirent*, the array holds pointers
			if (!nl) goto fail;
			if (list) memcpy(nl, list, n * sizeof *nl); // NOLINT(bugprone-sizeof-expression)
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

	/* NOLINTNEXTLINE(bugprone-sizeof-expression) -- list is struct dirent **,
	 * so *list is a pointer and sizeof *list is deliberately a pointer size:
	 * the array being sorted holds pointers, not structs. */
	if (compar) qsort_r(list, n, sizeof *list, scandir_cmp, (void *)compar);
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
