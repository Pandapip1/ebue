/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ftok(): a pure function of a file's identity, computed identically on
 * both backends -- it never touches a shmget()/msgget()/semget() table
 * at all, so unlike the rest of src/ipc/ it needs no linux/ or nt/
 * split. ftok.html DESCRIPTION: "the ftok() function shall return the
 * same key value for all paths that name the same file, when called
 * with the same id value" -- st_dev/st_ino, already the OS's own answer
 * to "which file is this", is exactly that: two paths naming the same
 * file produce the same (st_dev, st_ino) pair on either backend (see
 * src/stat/*.c), so folding those two fields plus the low 8 bits of id
 * into one key_t satisfies the contract without this file needing any
 * platform-specific knowledge of its own.
 *
 * The fold (id in the top byte, st_dev in the next, st_ino in the low
 * two) is the same layout glibc and musl both use. Matching it is not
 * required by POSIX -- "should return different key values ... with
 * paths that name different files" is a should, not a shall, so any
 * folding that keeps the common case collision-free would conform --
 * but a caller that has learned the shape empirically against another
 * XSI implementation gets the same answer here too.
 */
#include <sys/ipc.h>
#include <sys/stat.h>

key_t ftok(const char *path, int id)
{
	struct stat st;

	if (stat(path, &st) < 0)
		return (key_t)-1;
	return (key_t)(((unsigned)(id & 0xff) << 24) |
	               (((unsigned)st.st_dev & 0xff) << 16) |
	               ((unsigned)st.st_ino & 0xffff));
}
