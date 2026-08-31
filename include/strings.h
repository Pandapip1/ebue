/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

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
/* bcmp (src/string/bcmp.c) is a one-line forward to memcmp(), already
 * __pure__ (see string.h); index/rindex (src/string/index.c,
 * rindex.c) are the same one-line forwards to strchr()/strrchr(),
 * likewise already __pure__.  bcopy/bzero are real writers (they copy
 * into/zero their destination buffer) and are correctly left out. */
int bcmp (const void *, const void *, size_t) __attribute__((__pure__));
void bcopy (const void *, void *, size_t);
void bzero (void *, size_t);
char *index (const char *, int) __attribute__((__pure__));
char *rindex (const char *, int) __attribute__((__pure__));
#endif

/* ffs/ffsl/ffsll (src/string/ffs.c, ffsl.c, ffsll.c): pure bit
 * arithmetic over their one integer argument, no globals, no errno. */
#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_XOPEN_SOURCE)
int ffs (int) __attribute__((__pure__));
int ffsl (long) __attribute__((__pure__));
int ffsll (long long) __attribute__((__pure__));
#endif

/* strcasecmp's own loop (`*l && *r && (...)`) does short circuit r's
 * dereference on l's, but the unconditional `return tolower(*l) -
 * tolower(*r);` after it dereferences both regardless of how the loop
 * ended -- there is no early-return path in this function that skips
 * it, unlike strncasecmp's n == 0 case below. */
int strcasecmp (const char *, const char *) __attribute__((nonnull(1, 2), __pure__));
/* strncasecmp's `if (!n) return 0;` is a real, structural escape (n ==
 * 0 skips both pointers entirely, the same mem*-style convention);
 * once n >= 1, the same unconditional post-loop `return tolower(*l) -
 * tolower(*r);` as strcasecmp applies. Reads only, via tolower() which
 * is itself __pure__ (ctype.h). */
int strncasecmp (const char *, const char *, size_t) __attribute__((nonnull(1, 2), __pure__));

/* strcasecmp_l/strncasecmp_l simply forward to strcasecmp/strncasecmp
 * above, ignoring their own locale_t (src/string/strcasecmp.c:
 * `(void)loc;` -- this tree's whole wctype/locale family is
 * ASCII-only C/POSIX, so there is no second behaviour to select),
 * inheriting the same requirement on their string arguments and the
 * same __pure__ reasoning. */
int strcasecmp_l (const char *, const char *, locale_t) __attribute__((nonnull(1, 2), __pure__));
int strncasecmp_l (const char *, const char *, size_t, locale_t) __attribute__((nonnull(1, 2), __pure__));

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
