/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * flock(2), BSD whole-file advisory-on-Unix locking:
 * https://man7.org/linux/man-pages/man2/flock.2.html
 *
 * fcntl(F_GETLK/F_SETLK/F_SETLKW)'s POSIX record locks (struct flock,
 * declared in include/fcntl.h) and this interface are both backed by NT
 * byte-range locks.  Consequently they can conflict here, unlike Linux,
 * where flock() and fcntl() occupy separate lock spaces.  flock() still
 * keeps its own descriptor-level state in src/file/flock.c because its
 * whole-file conversion rules differ from fcntl()'s arbitrary ranges.
 *
 * Advisory vs. mandatory: **mandatory**, not advisory, despite the
 * name this header is conventionally filed under. NT byte-range locks
 * (LockFile()/LockFileEx(), which NtLockFile() is the ntdll primitive
 * under) are enforced by the filesystem against *every* handle to the
 * region, not merely against callers that also call flock():
 * "Locking a region of a file gives the threads of the locking process
 * exclusive access to the specified region using this file handle...
 * If the locking process opens the file a second time, it cannot
 * access the specified region through this second handle until it
 * unlocks the region." (LockFile function (fileapi.h), Win32 apps |
 * Microsoft Learn, Remarks). A second handle -- from this process or
 * any other, flock()-aware or not -- that tries to read or write the
 * locked region gets a real I/O failure (mapped to EWOULDBLOCK here,
 * src/internal/errno.c's STATUS_LOCK_NOT_GRANTED/
 * STATUS_FILE_LOCK_CONFLICT entries), not silence. This is the
 * opposite of POSIX flock()'s documented advisory contract ("a process
 * is free to ignore the use of flock() and perform I/O on the file" --
 * flock(2) NOTES); a program written to POSIX's advisory assumption
 * (relying on an uncooperative reader/writer being unaffected) will
 * observe stricter behaviour here, never looser.
 */
#ifndef _SYS_FILE_H
#define _SYS_FILE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

/* flock(2) DESCRIPTION. Values match glibc's and *BSD's -- nothing
 * standardizes them, but plenty of existing source hardcodes the
 * numeric literal instead of the macro, so matching costs nothing. */
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
