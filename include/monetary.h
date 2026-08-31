/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <monetary.h>: monetary formatting.  See src/misc/strfmon.c.
 *
 * monetary.h.html DESCRIPTION, verbatim: "The <monetary.h> header
 * shall define the locale_t type as described in <locale.h>.", "The
 * <monetary.h> header shall define the size_t type as described in
 * <stddef.h>.", "The <monetary.h> header shall define the ssize_t type
 * as described in <sys/types.h>." -- and the two prototypes below.
 *
 * What a caller gets here, stated once so it is not a surprise: the
 * mechanics of the conversion specification are implemented in full --
 * the =f fill character, the '^', '+', '(', '!' and '-' flags, field
 * width, left precision #n, right precision .p, "%%", and the [E2BIG]
 * truncation rule.  The *locale data* those mechanics format with is
 * the POSIX locale's, and the POSIX locale's entire LC_MONETARY block
 * is the "not available" value: no currency symbol, no grouping, no
 * sign strings, {CHAR_MAX} for every numeric field.  So %i and %n here
 * differ from each other, and from "%.2f", by very little.  That is a
 * property of the only locale this library has, not of this
 * implementation, and src/misc/strfmon.c names every place it has to
 * choose a fallback because the locale said "not available".
 */
#ifndef _MONETARY_H
#define _MONETARY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_size_t
#define __NEED_ssize_t
#define __NEED_locale_t

#include <bits/alltypes.h>

/* fmt required -- forwarded, unguarded, into the static vstrfmon() in
 * src/misc/strfmon.c, itself required at its own now-explicit
 * contract (dereferenced unconditionally by the format-scan loop). s
 * is deliberately NOT marked; see the vstrfmon() comment for the one
 * real (if incidental) path where a NULL s does not crash. */
ssize_t strfmon(char *__restrict, size_t, const char *__restrict, ...)
    __attribute__((nonnull(3)));
ssize_t strfmon_l(char *__restrict, size_t, locale_t,
                  const char *__restrict, ...) __attribute__((nonnull(4)));

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
