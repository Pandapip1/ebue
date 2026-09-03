/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <fnmatch.h>: pure pattern matching against the XBD 9.13 "Pattern
 * Matching Notation" grammar. No OS dependency; see src/fnmatch/fnmatch.c.
 *
 * Flag values must match test/posix-glob.c's own local copies, which it
 * declares independently of this header and calls fnmatch() through.
 */
#ifndef _FNMATCH_H
#define _FNMATCH_H
#ifdef __cplusplus
extern "C" {
#endif

#define FNM_PATHNAME	0x1	/* '/' in string only matched by literal '/' in pattern */
#define FNM_NOESCAPE	0x2	/* backslash is an ordinary character, not an escape */
#define FNM_PERIOD	0x4	/* leading '.' must be matched explicitly */

#define FNM_NOMATCH	1

int fnmatch(const char *, const char *, int) __attribute__((nonnull(1, 2), __pure__));

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
