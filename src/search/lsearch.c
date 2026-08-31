/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * lsearch/lfind: linear scan over a caller-owned array. lfind() never
 * writes; lsearch() appends a miss to *base[*nelp] and bumps *nelp
 * (lsearch.html/lfind.html DESCRIPTION).
 */
#include <search.h>
#include <string.h>

void *lfind(const void *key, const void *base, size_t *nelp, size_t width,
	    int (*compar)(const void *, const void *))
{
	const char *p = base;
	size_t i;

	for (i = 0; i < *nelp; i++, p += width)
		if (compar(key, p) == 0) return (void *)p;
	return NULL;
}

void *lsearch(const void *key, void *base, size_t *nelp, size_t width,
	      int (*compar)(const void *, const void *))
{
	void *found = lfind(key, base, nelp, width, compar);
	char *slot;

	if (found) return found;

	slot = (char *)base + *nelp * width;
	memcpy(slot, key, width);
	(*nelp)++;
	return slot;
}
