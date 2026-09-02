/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef	_CTYPE_H
#define	_CTYPE_H

#include <features.h>

/* basedefs/ctype.h.html DESCRIPTION: "The <ctype.h> header shall define
 * the locale_t type as described in <locale.h>."  That sentence is
 * unconditional -- not gated behind any of the _POSIX_SOURCE/XOPEN/GNU
 * feature-test macros locale.h itself gates newlocale() et al. behind
 * -- so the request for the type is unconditional here too. */
#define __NEED_locale_t

#include <bits/alltypes.h>

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
 * observable in between.  Marked __attribute__((__pure__)) accordingly.
 *
 * Deliberately ASCII-only for c in 0x80-0xff, even though wctype.h's
 * isw*() family is now backed by real Unicode data (see that header's
 * own banner comment): this is not a limitation these two families
 * happen to still share, it is the UTF-8-correct answer. ntlibc's char*
 * strings are UTF-8 (src/internal/utf.c), and in UTF-8 a single byte in
 * 0x80-0xff is never a complete character on its own -- it is always
 * either a continuation byte (0x80-0xbf) or a multi-byte sequence's
 * lead byte (0xc0-0xf4), and isalpha(int) et al. only ever see one byte
 * at a time, with no way to look at its neighbours. There is no decoded
 * code point for that lone byte to classify: "false" is correct, not
 * merely consistent with a locale position. Handing these functions a
 * decoded code point instead -- e.g. isalpha(0xe9) for "é" -- is exactly
 * the case iswalpha(0xe9) exists to answer instead. */
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

/* The _l family (isalnum_l() ... toupper_l(), CX in this edition):
 * ctype.h.html's own DESCRIPTION says each "shall be equivalent to
 * ... except that the effect ... in the locale represented by locale
 * is used instead of the current locale."  ntlibc's locale.c accepts
 * exactly one locale ("C"/"POSIX" -- setlocale() rejects every other
 * name, and newlocale() in locale.h hands out the address of that same
 * one static object for every valid request), so "the locale
 * represented by locale" and "the current locale" are always the same
 * classification table.  Each _l function below is therefore its
 * non-_l sibling above, taking and ignoring a locale_t argument --
 * documented, not silent, exactly the strcasecmp_l()/strncasecmp_l()
 * precedent in strings.h (see src/ctype/isalpha.c etc. for the `(void)
 * loc;` bodies).  Still __pure__: the ignored locale_t is never
 * dereferenced, so nothing changes about totality or the absence of
 * side effects. */
int   isalnum_l(int, locale_t) __attribute__((__pure__));
int   isalpha_l(int, locale_t) __attribute__((__pure__));
int   isblank_l(int, locale_t) __attribute__((__pure__));
int   iscntrl_l(int, locale_t) __attribute__((__pure__));
int   isdigit_l(int, locale_t) __attribute__((__pure__));
int   isgraph_l(int, locale_t) __attribute__((__pure__));
int   islower_l(int, locale_t) __attribute__((__pure__));
int   isprint_l(int, locale_t) __attribute__((__pure__));
int   ispunct_l(int, locale_t) __attribute__((__pure__));
int   isspace_l(int, locale_t) __attribute__((__pure__));
int   isupper_l(int, locale_t) __attribute__((__pure__));
int   isxdigit_l(int, locale_t) __attribute__((__pure__));
int   tolower_l(int, locale_t) __attribute__((__pure__));
int   toupper_l(int, locale_t) __attribute__((__pure__));

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
