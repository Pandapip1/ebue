/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <crypt.h>: the traditional (glibc/BSD, not POSIX-standardized) home
 * for crypt()'s declaration, separate from crypt()'s own XSI listing in
 * <unistd.h> (both declare the same function; a translation unit is
 * free to pick up either or both). src/unistd/crypt.c has the
 * implementation and algorithm notes.
 */
#ifndef _CRYPT_H
#define _CRYPT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

/* Same nonnull rationale as the <unistd.h> declaration this mirrors. */
char *crypt(const char *, const char *) __attribute__((nonnull(1, 2)));

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
