/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wcsdup(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/wcsdup.html
 * DESCRIPTION, RETURN VALUE -- "a pointer to a new wide-character
 * string, which is a duplicate of the wide-character string pointed to
 * by string", allocated as if by malloc() and freed with free(); a
 * null pointer and [ENOMEM] on failure.  The wchar_t mirror of
 * strdup() (src/string/strdup.c).
 *
 * The byte count is (wcslen+1) * sizeof(wchar_t), not wcslen+1: this
 * is the one place the wide/narrow transliteration is not literal, and
 * getting it wrong would allocate half the memory needed.  malloc()
 * already sets ENOMEM on failure, so nothing extra is done here.
 */
#include <wchar.h>
#include <stdlib.h>
#include <string.h>
#include "ownership_stubs.h"

withtok(heap_allocated)
wchar_t *wcsdup(const wchar_t *s)
{
	size_t n = (wcslen(s) + 1) * sizeof(wchar_t);
	wchar_t *d = malloc(n);
	if (!d) return 0;
	__ownership_readable_span(s, n);
	return memcpy(d, s, n);
}
