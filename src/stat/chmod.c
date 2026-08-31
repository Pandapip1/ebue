/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* chmod persists the POSIX mode in WSL's $LXMOD NTFS extended attribute.
 * FILE_ATTRIBUTE_READONLY is kept in sync with the aggregate write bits so
 * ordinary Windows programs observe the closest native equivalent too. */
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include "libc.h"

static int chmod_handle(HANDLE h, mode_t mode)
{
	IO_STATUS_BLOCK io;
	FILE_BASIC_INFORMATION bi, set;
	struct stat before;
	NTSTATUS st;
	unsigned lxmode;
	int have_before = __fstat_handle(h, __FD_FILE, &before) == 0;
	st = NtQueryInformationFile(h, &io, &bi, sizeof bi, FileBasicInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	lxmode = (bi.FileAttributes & FILE_ATTRIBUTE_DIRECTORY ? S_IFDIR : S_IFREG) |
	         (mode & 07777);
	/* Unlike `bi` above (fully populated by the kernel's own query), `set`
	 * is a fresh local this function fills itself: FILE_BASIC_INFORMATION
	 * mixes four 8-byte LARGE_INTEGER fields with a trailing 4-byte ULONG,
	 * so a target that pads the struct out to 8-byte alignment leaves
	 * real uninitialized bytes after FileAttributes even once every named
	 * field below is set. */
	memset(&set, 0, sizeof set);
	set.CreationTime = set.LastAccessTime = set.LastWriteTime = set.ChangeTime = 0;
	set.FileAttributes = bi.FileAttributes;
	if (mode & 0222) set.FileAttributes &= ~FILE_ATTRIBUTE_READONLY;
	else set.FileAttributes |= FILE_ATTRIBUTE_READONLY;
	/* FILE_ATTRIBUTE_NORMAL is valid only by itself.  A queried plain file
	 * commonly carries NORMAL, so adding READONLY without removing NORMAL
	 * asks NT to ignore the very transition chmod() is making.  Conversely,
	 * clearing READONLY from an archived file must leave ARCHIVE alone rather
	 * than manufacture the invalid ARCHIVE|NORMAL pair. */
	if (set.FileAttributes & ~FILE_ATTRIBUTE_NORMAL)
		set.FileAttributes &= ~FILE_ATTRIBUTE_NORMAL;
	else
		set.FileAttributes = FILE_ATTRIBUTE_NORMAL;
	if (set.FileAttributes != bi.FileAttributes) {
		st = NtSetInformationFile(h, &io, &set, sizeof set, FileBasicInformation);
		if (!NT_SUCCESS(st)) return __set_errno_status(st);
	}
	if (__lxmod_set(h, lxmode) < 0) {
		/* Wine through 10.x stubs NtSetEaFile as ACCESS_DENIED.  Preserve
		 * the historical readonly-only chmod when the requested execute
		 * bits already match what stat could report without $LXMOD; an
		 * actual execute-bit change still fails honestly. */
		if (have_before && (mode & 0111) == (before.st_mode & 0111))
			return 0;
		/* Do not leave the Windows read-only state changed after a mode
		 * metadata failure.  The old $LXMOD value, if any, was untouched
		 * because NtSetEaFile replaces its single entry atomically. */
		if (set.FileAttributes != bi.FileAttributes)
			NtSetInformationFile(h, &io, &bi, sizeof bi, FileBasicInformation);
		return -1;
	}
	return 0;
}

int fchmod(int fd, mode_t mode)
{
	struct __fd *f = __fd_get(fd);
	char *path;
	int r, e;
	if (!f) return -1;
	if (f->vfs && !f->vfs_native) { errno = EROFS; return -1; }
	if (f->type != __FD_FILE && f->type != __FD_DIR) return 0;
	r = chmod_handle(f->h, mode);
	if (r == 0 || errno != EACCES) return r;
	/* O_RDONLY handles do not carry FILE_WRITE_EA.  Reopen the same object
	 * by name with that right, as path-taking chmod does.  An unlinked file
	 * has no reopenable name and correctly retains the original failure. */
	e = errno;
	path = __handle_path(f->h);
	if (!path) { errno = e; return -1; }
	r = fchmodat(AT_FDCWD, path, mode, 0);
	free(path);
	return r;
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
	st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES |
	               FILE_READ_EA | FILE_WRITE_EA | SYNCHRONIZE,
	               &np.oa, &io, FILE_SHARE_VALID_FLAGS, options);
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
		st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | FILE_READ_EA | SYNCHRONIZE,
		                &np.oa, &io, FILE_SHARE_VALID_FLAGS, options);
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
