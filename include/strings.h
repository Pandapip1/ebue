/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef	_STRINGS_H
#define	_STRINGS_H

#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __NEED_size_t
#define __NEED_locale_t
#include <bits/alltypes.h>

#if (!defined(_POSIX_SOURCE) && !defined(_POSIX_C_SOURCE) \
 && !defined(_XOPEN_SOURCE)) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE) || (defined(_XOPEN_SOURCE) && _XOPEN_SOURCE < 700)
int bcmp (const void *, const void *, size_t);
void bcopy (const void *, void *, size_t);
void bzero (void *, size_t);
char *index (const char *, int);
char *rindex (const char *, int);
#endif

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_XOPEN_SOURCE)
int ffs (int);
int ffsl (long);
int ffsll (long long);
#endif

/* strcasecmp's own loop (`*l && *r && (...)`) does short circuit r's
 * dereference on l's, but the unconditional `return tolower(*l) -
 * tolower(*r);` after it dereferences both regardless of how the loop
 * ended -- there is no early-return path in this function that skips
 * it, unlike strncasecmp's n == 0 case below. */
int strcasecmp (const char *, const char *) __attribute__((nonnull(1, 2)));
/* strncasecmp's `if (!n) return 0;` is a real, structural escape (n ==
 * 0 skips both pointers entirely, the same mem*-style convention);
 * once n >= 1, the same unconditional post-loop `return tolower(*l) -
 * tolower(*r);` as strcasecmp applies. */
int strncasecmp (const char *, const char *, size_t) __attribute__((nonnull(1, 2)));

/* strcasecmp_l/strncasecmp_l simply forward to strcasecmp/strncasecmp
 * above, ignoring their own locale_t (src/string/strcasecmp.c:
 * `(void)loc;` -- this tree's whole wctype/locale family is
 * ASCII-only C/POSIX, so there is no second behaviour to select),
 * inheriting the same requirement on their string arguments. */
int strcasecmp_l (const char *, const char *, locale_t) __attribute__((nonnull(1, 2)));
int strncasecmp_l (const char *, const char *, size_t, locale_t) __attribute__((nonnull(1, 2)));

#ifdef __cplusplus
}
#endif

#endif
