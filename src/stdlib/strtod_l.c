/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * strtof_l/strtod_l/strtold_l: the _l family exists so a caller can pick
 * a locale without disturbing the thread's global one.  This library has
 * no locale objects to pick between -- include/stdlib.h forward-declares
 * `struct __locale_struct` only so these three prototypes can name a
 * pointer to it, and nothing ever defines the struct or constructs one
 * (there is no newlocale()/uselocale() here) -- and its numeric parsing
 * is not locale-sensitive to begin with (src/stdlib/strtod.c parses a
 * plain '.' radix point unconditionally, the only thing LC_NUMERIC could
 * change).  So the locale argument has nothing to select between; these
 * are exactly strtof/strtod/strtold with an ignored extra parameter,
 * which is the same conclusion glibc's own C-locale fast path and musl
 * both reach for a "C"-only libc.
 */
#include <stdlib.h>

struct __locale_struct;

float strtof_l(const char *__restrict s, char **__restrict end, struct __locale_struct *loc)
{
	(void)loc;
	return strtof(s, end);
}

double strtod_l(const char *__restrict s, char **__restrict end, struct __locale_struct *loc)
{
	(void)loc;
	return strtod(s, end);
}

long double strtold_l(const char *__restrict s, char **__restrict end, struct __locale_struct *loc)
{
	(void)loc;
	return strtold(s, end);
}
