/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"

int mkdirat(int dirfd, const char *path, mode_t mode)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	(void)mode;

	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	st = NtCreateFile(&h, FILE_LIST_DIRECTORY | SYNCHRONIZE, &np.oa, &io, 0, FILE_ATTRIBUTE_NORMAL,
	                  FILE_SHARE_VALID_FLAGS, FILE_CREATE,
	                  FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT, 0, 0);
	__ntpath_free(&np);
	/* mkdir.html requires [EEXIST] when the named file exists, whatever
	 * kind of file it is -- and this call reaches that case through NT's
	 * *collision* status rather than through a type mismatch, which is
	 * not obvious and is worth pinning down.
	 *
	 * The call pairs FILE_CREATE with FILE_DIRECTORY_FILE, so an existing
	 * plain file at `path` is both a name collision and a create-option
	 * mismatch, and NT has to pick one.  Measured on Windows 11 Pro 22621,
	 * NTFS, by the Wine-divergence session: it reports the collision,
	 * 0xc0000035, NOT STATUS_NOT_A_DIRECTORY.  So this arm fires and the
	 * errno is right.
	 *
	 * NT is asymmetric here and there is no tidier rule to remember: the
	 * mirror case -- FILE_NON_DIRECTORY_FILE against an existing
	 * directory, which is what src/unistd/link.c's symlinkat() issues --
	 * reports the *mismatch* first, 0xc00000ba, and link.c maps that to
	 * EEXIST separately for the same POSIX reason.  Both are genuine
	 * mismatches; only one beats the collision.
	 *
	 * So do not "simplify" by assuming the two call sites can share one
	 * status arm, and do not add a STATUS_NOT_A_DIRECTORY case here on the
	 * theory that NT is consistent about which it reports.  ReactOS's NTFS
	 * driver had the opposite ordering and was corrected to match NT
	 * (reactos-divergences 7ee3248c); had it instead been "fixed" to check
	 * both options before the disposition -- the symmetric-looking change
	 * -- this function would have started returning the wrong errno there. */
	if (st == STATUS_OBJECT_NAME_COLLISION) { errno = EEXIST; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	NtClose(h);
	return 0;
}

int mkdir(const char *path, mode_t mode) { return mkdirat(AT_FDCWD, path, mode); }
