/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * unlink and rmdir: open for DELETE and set the disposition.  POSIX
 * semantics (the name goes away at once even while other handles are
 * open) are asked for first, on Windows 10 1709 and later; older systems
 * answer STATUS_INVALID_PARAMETER and get the classic delete-on-close.
 */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"

int __unlink_at(int dirfd, const char *path, int isdir)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	FILE_DISPOSITION_INFORMATION_EX dx;
	FILE_DISPOSITION_INFORMATION d;
	FILE_BASIC_INFORMATION bi;

	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	st = NtOpenFile(&h, DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS,
	                FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT | FILE_OPEN_FOR_BACKUP_INTENT |
	                (isdir ? FILE_DIRECTORY_FILE : FILE_NON_DIRECTORY_FILE));
	__ntpath_free(&np);
	if (st == STATUS_FILE_IS_A_DIRECTORY) { errno = EISDIR; return -1; }
	if (st == STATUS_NOT_A_DIRECTORY) { errno = ENOTDIR; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	/* A read-only file cannot be deleted until the attribute is cleared;
	 * Unix has no such notion, so clear it. */
	if (NT_SUCCESS(NtQueryInformationFile(h, &io, &bi, sizeof bi, FileBasicInformation)) &&
	    (bi.FileAttributes & FILE_ATTRIBUTE_READONLY)) {
		FILE_BASIC_INFORMATION set = {0};
		set.FileAttributes = (bi.FileAttributes & ~FILE_ATTRIBUTE_READONLY) | ((bi.FileAttributes & ~FILE_ATTRIBUTE_READONLY) ? 0 : FILE_ATTRIBUTE_NORMAL);
		NtSetInformationFile(h, &io, &set, sizeof set, FileBasicInformation);
	}

	dx.Flags = FILE_DISPOSITION_DELETE | FILE_DISPOSITION_POSIX_SEMANTICS;
	st = NtSetInformationFile(h, &io, &dx, sizeof dx, FileDispositionInformationEx);
	if (st == STATUS_INVALID_PARAMETER || st == STATUS_INVALID_INFO_CLASS || st == STATUS_NOT_SUPPORTED || st == STATUS_NOT_IMPLEMENTED) {
		d.DeleteFile = 1;
		st = NtSetInformationFile(h, &io, &d, sizeof d, FileDispositionInformation);
	}
	NtClose(h);
	if (st == STATUS_DIRECTORY_NOT_EMPTY) { errno = ENOTEMPTY; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int unlink(const char *path) { return __unlink_at(AT_FDCWD, path, 0); }
int rmdir(const char *path) { return __unlink_at(AT_FDCWD, path, 1); }

/* AT_REMOVEDIR is the only flag unlinkat() defines, and unlink.html's
 * "[EINVAL] (unlinkat() only) The value of the flag argument is not
 * valid" is a shall-fail: every other bit has to be refused rather than
 * masked off, or a caller who passes the wrong AT_* constant gets a
 * deletion instead of a diagnostic. */
int unlinkat(int dirfd, const char *path, int flags)
{
	if (flags & ~AT_REMOVEDIR) { errno = EINVAL; return -1; }
	return __unlink_at(dirfd, path, flags & AT_REMOVEDIR);
}
