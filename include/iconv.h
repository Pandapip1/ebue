/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <iconv.h>: codeset conversion.  See src/misc/iconv.c.
 *
 * iconv.h.html DESCRIPTION, verbatim: "The <iconv.h> header shall
 * define the following types:" -- iconv_t, "Identifies the conversion
 * from one codeset to another.", and size_t -- then the three
 * prototypes below.
 *
 * WHAT CONVERSIONS EXIST HERE.  iconv_open.html says "Settings of
 * fromcode and tocode and their permitted combinations are
 * implementation-defined", so this is ntlibc's choice and not a
 * requirement: UTF-8 and UTF-16LE, in all four combinations (including
 * each to itself, which is a validating copy and not a memcpy).  Those
 * two are what this library already converts between internally for
 * every path it hands to ntdll -- every char* is UTF-8 and every
 * UNICODE_STRING is UTF-16 -- so they are the pair a caller on this
 * platform most needs a portable name for.  Anything else is refused
 * with (iconv_t)-1 and EINVAL, which iconv_open.html provides for:
 * "[EINVAL] The conversion specified by fromcode and tocode is not
 * supported by the implementation."
 *
 * Codeset names are matched case-insensitively and ignoring '-' and
 * '_', so "UTF-8", "utf8" and "UTF_8" are the same name.  "UCS-2LE" is
 * NOT accepted as a spelling of UTF-16LE: UCS-2 cannot represent a
 * supplementary character and this converter emits surrogate pairs, so
 * accepting the name would misdescribe what comes out.
 */
#ifndef _ICONV_H
#define _ICONV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <allocation_tokens.h>
#include <ownership.h>

#define __NEED_size_t

#include <bits/alltypes.h>

typedef void *iconv_t;

tokdef iconv_opened
	dynamic_storage
	implemented_by(heap_allocated)
	sentinel_exclude(-1);

withtok(iconv_opened)
iconv_t iconv_open(const char *, const char *);
size_t  iconv(iconv_t, char **__restrict, size_t *__restrict,
              char **__restrict, size_t *__restrict);
int     iconv_close(iconv_t consume(iconv_opened));

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
