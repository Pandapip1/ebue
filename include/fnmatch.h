/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <fnmatch.h>: pure pattern matching against the XBD 9.13 "Pattern
 * Matching Notation" grammar -- '?', '*', bracket expressions (ranges,
 * negation via '!' or '^', and [:class:] names), and backslash
 * escaping.  No OS dependency at all; see src/fnmatch/fnmatch.c for the
 * matcher itself.
 *
 * Flag/FNM_NOMATCH values are fixed at test/posix-glob.c's choices (it
 * predates this header and declares its own local copies of them,
 * unmodified, per that file's file-header convention): FNM_PATHNAME
 * 0x1, FNM_NOESCAPE 0x2, FNM_PERIOD 0x4, FNM_NOMATCH 1.  Since that
 * test file calls fnmatch() through its own locally declared prototype
 * rather than this header, the flag bit values below have to agree
 * with its copies for a flags argument built from one file's macros to
 * mean the same thing inside the other's fnmatch() -- POSIX itself
 * only requires FNM_NOMATCH be "a defined constant" distinct from 0,
 * nothing more.
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

/* pattern/string are both required: src/fnmatch/fnmatch.c forwards
 * them straight into fnm_match(), which itself dereferences both
 * unconditionally. */
int fnmatch(const char *, const char *, int) __attribute__((nonnull(1, 2)));

#ifdef __cplusplus
}
#endif
#endif
