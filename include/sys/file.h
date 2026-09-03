/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * flock(2), BSD whole-file advisory locking.
 *
 * fcntl(F_GETLK/F_SETLK/F_SETLKW)'s POSIX record locks and this interface
 * are both backed by NT byte-range locks, so they can conflict here,
 * unlike Linux where they occupy separate lock spaces.
 *
 * Locking here is **mandatory**, not advisory, despite POSIX's documented
 * advisory contract: NT byte-range locks are enforced by the filesystem
 * against every handle to the region, not merely against cooperating
 * callers. A second handle that tries to read or write the locked region
 * gets a real I/O failure (mapped to EWOULDBLOCK), not silence. A program
 * relying on an uncooperative reader/writer being unaffected will observe
 * stricter behavior here, never looser.
 */
#ifndef _SYS_FILE_H
#define _SYS_FILE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

/* Values match glibc's and *BSD's; nothing standardizes them, but plenty
 * of existing source hardcodes the numeric literal instead of the macro. */
#define LOCK_SH 1   /* shared lock */
#define LOCK_EX 2   /* exclusive lock */
#define LOCK_NB 4   /* modifier: don't block (ORed with LOCK_SH/LOCK_EX) */
#define LOCK_UN 8   /* unlock */

int flock(int, int);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
