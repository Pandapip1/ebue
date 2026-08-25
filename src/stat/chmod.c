/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* chmod can only express one thing on NTFS: whether the file is read-only. */
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"

static int chmod_handle(HANDLE h, mode_t mode)
{
	IO_STATUS_BLOCK io;
	FILE_BASIC_INFORMATION bi, set;
	NTSTATUS st;
	st = NtQueryInformationFile(h, &io, &bi, sizeof bi, FileBasicInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	set.CreationTime = set.LastAccessTime = set.LastWriteTime = set.ChangeTime = 0;
	set.FileAttributes = bi.FileAttributes;
	if (mode & 0222) set.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
	else set.FileAttributes |= FILE_ATTRIBUTE_READONLY;
	if (!(set.FileAttributes & ~FILE_ATTRIBUTE_ARCHIVE)) set.FileAttributes |= FILE_ATTRIBUTE_NORMAL;
	if (set.FileAttributes == bi.FileAttributes) return 0;
	st = NtSetInformationFile(h, &io, &set, sizeof set, FileBasicInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int fchmod(int fd, mode_t mode)
{
	struct __fd *f = __fd_get(fd);
	if (!f) return -1;
	if (f->type != __FD_FILE && f->type != __FD_DIR) return 0;
	return chmod_handle(f->h, mode);
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	ULONG options;
	int r;
	/* fchmodat.html ERRORS: "[EINVAL] The value of the flag argument is
	 * invalid."  AT_SYMLINK_NOFOLLOW is the only flag this page defines,
	 * so every other bit is invalid.  It is a *may fail* error, so
	 * ignoring the bits was conforming -- but silently succeeding on a
	 * flag the caller believes it asked for is the worse of the two legal
	 * answers, and glibc reports EINVAL here (measured: fchmodat(AT_FDCWD,
	 * path, 0644, 0x4000) -> -1/EINVAL, while AT_SYMLINK_NOFOLLOW and 0
	 * both succeed).  Checked before __ntpath_at() so a bad flag costs no
	 * path conversion and cannot be masked by a path error. */
	if (flags & ~AT_SYMLINK_NOFOLLOW) { errno = EINVAL; return -1; }
	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	options = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT | (flags & AT_SYMLINK_NOFOLLOW ? FILE_OPEN_REPARSE_POINT : 0);
	st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS, options);
	if (st == STATUS_ACCESS_DENIED) {
		/* chmod.html DESCRIPTION: the owner of a file "may always
		 * change the permission of the file" -- a file's own mode
		 * must never itself forbid chmod().  Wine's server denies
		 * a FILE_WRITE_ATTRIBUTES open outright when the file
		 * already carries FILE_ATTRIBUTE_READONLY (real NT does
		 * not; see test/posix-unistd.c's test_open_umask_bug()),
		 * which would otherwise make a 0444 file permanently
		 * un-chmod-able by path.  Fall back to a handle that only
		 * asks to read attributes -- Wine's NtSetInformationFile
		 * does not itself require FILE_WRITE_ATTRIBUTES on the
		 * handle, the same workaround test/unistd.c already applies
		 * by hand via fchmod() on an O_RDONLY descriptor. */
		st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS, options);
	}
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	r = chmod_handle(h, mode);
	NtClose(h);
	return r;
}

int chmod(const char *path, mode_t mode) { return fchmodat(AT_FDCWD, path, mode, 0); }

static mode_t umask_value = 022;
mode_t umask(mode_t m) { mode_t o = umask_value; umask_value = m & 0777; return o; }
unsigned __umask_get(void) { return umask_value; }

int mkfifo(const char *p, mode_t m) { (void)p; (void)m; errno = ENOSYS; return -1; }
int mkfifoat(int d, const char *p, mode_t m) { (void)d; (void)p; (void)m; errno = ENOSYS; return -1; }
int mknod(const char *p, mode_t m, dev_t dv) { (void)p; (void)m; (void)dv; errno = EPERM; return -1; }
int mknodat(int d, const char *p, mode_t m, dev_t dv) { (void)d; (void)p; (void)m; (void)dv; errno = EPERM; return -1; }
