/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef	_CTYPE_H
#define	_CTYPE_H

#include <features.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Every function in this header is pure arithmetic on `(unsigned)c`
 * (see the .c files under src/ctype/): no table lookup, no locale
 * read (src/misc/locale.c's setlocale() never accepts any locale but
 * "C"/"POSIX", so there is no second classification table this could
 * ever pick), no errno, no global/static state, no I/O.  Each is a
 * total function of its one int argument -- calling any of them twice
 * with the same c always gives the same answer, with nothing else
 * observable in between.  Marked __attribute__((__pure__)) accordingly. */
int   isalnum(int) __attribute__((__pure__));
int   isalpha(int) __attribute__((__pure__));
int   isblank(int) __attribute__((__pure__));
int   iscntrl(int) __attribute__((__pure__));
int   isdigit(int) __attribute__((__pure__));
int   isgraph(int) __attribute__((__pure__));
int   islower(int) __attribute__((__pure__));
int   isprint(int) __attribute__((__pure__));
int   ispunct(int) __attribute__((__pure__));
int   isspace(int) __attribute__((__pure__));
int   isupper(int) __attribute__((__pure__));
int   isxdigit(int) __attribute__((__pure__));
int   tolower(int) __attribute__((__pure__));
int   toupper(int) __attribute__((__pure__));

#if defined(_XOPEN_SOURCE) || defined(_GNU_SOURCE) || defined(_BSD_SOURCE)
int   isascii(int) __attribute__((__pure__));
int   toascii(int) __attribute__((__pure__));
#define _tolower(a) ((a)|0x20)
#define _toupper(a) ((a)&0x5f)
#endif

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
