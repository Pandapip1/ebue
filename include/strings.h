/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef	_STRINGS_H
#define	_STRINGS_H

#include <features.h>
#include <memory_tokens.h>
#include <string_tokens.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __NEED_size_t
#define __NEED_locale_t
#include <bits/alltypes.h>

#if (!defined(_POSIX_SOURCE) && !defined(_POSIX_C_SOURCE) \
 && !defined(_XOPEN_SOURCE)) || defined(_GNU_SOURCE) \
 || defined(_BSD_SOURCE) || (defined(_XOPEN_SOURCE) && _XOPEN_SOURCE < 700)
/* bcopy/bzero are real writers and correctly not marked __pure__. */
int bcmp (const void *s1 withtok(readable_span(n)),
          const void *s2 withtok(readable_span(n)), size_t n)
    __attribute__((__pure__));
void bcopy (const void *s1 withtok(readable_span(n)),
            void *s2 withtok(writable_span(n)), size_t n);
void bzero (void *s withtok(writable_span(n)), size_t n);
withtok(null_terminated)
char *index (const char * withtok(null_terminated), int) __attribute__((__pure__));
withtok(null_terminated)
char *rindex (const char * withtok(null_terminated), int) __attribute__((__pure__));
#endif

#if defined(_GNU_SOURCE) || defined(_BSD_SOURCE) || defined(_XOPEN_SOURCE)
int ffs (int) __attribute__((__pure__));
int ffsl (long) __attribute__((__pure__));
int ffsll (long long) __attribute__((__pure__));
#endif

int strcasecmp (const char * withtok(null_terminated),
                const char * withtok(null_terminated))
    __attribute__((nonnull(1, 2), __pure__));
int strncasecmp (const char *, const char *, size_t) __attribute__((nonnull(1, 2), __pure__));

/* strcasecmp_l/strncasecmp_l forward to strcasecmp/strncasecmp above,
 * ignoring locale_t: this tree's locale family is ASCII-only C/POSIX,
 * so there is no second behavior to select. */
int strcasecmp_l (const char * withtok(null_terminated),
                  const char * withtok(null_terminated), locale_t)
    __attribute__((nonnull(1, 2), __pure__));
int strncasecmp_l (const char *, const char *, size_t, locale_t) __attribute__((nonnull(1, 2), __pure__));

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
