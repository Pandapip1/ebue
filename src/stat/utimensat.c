/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include "libc.h"

static int set_times_handle(HANDLE h, const struct timespec ts[2])
{
	IO_STATUS_BLOCK io;
	FILE_BASIC_INFORMATION bi;
	LARGE_INTEGER now;
	NTSTATUS st;

	/* utime.html DESCRIPTION says only the access/modification times
	 * change -- the mode must survive untouched.  FILE_BASIC_INFORMATION
	 * documents FileAttributes==0 as "leave the attributes alone"; real NT
	 * honors that, so rely on it directly rather than round-tripping
	 * through NtQueryInformationFile first.  A prior version of this
	 * function queried the current attributes and passed them back
	 * explicitly to work around a Wine bug (NtSetInformationFile there
	 * did not honor FileAttributes==0 and silently cleared
	 * FILE_ATTRIBUTE_READONLY on every timestamp-only call).  That query
	 * required FILE_READ_ATTRIBUTES on the handle, which utimensat()'s
	 * open below never requests -- Wine does not enforce the access
	 * check on FileBasicInformation queries, but real NT does, so the
	 * query failed with STATUS_ACCESS_DENIED on every ordinary file on
	 * real Windows. The Wine bug is now fixed at the source (Wine
	 * honors FileAttributes==0), so the workaround is no longer needed
	 * here; see wine commit "ntdll: Honor FileAttributes==0 (\"leave
	 * unchanged\") in NtSetInformationFile." */
	bi.CreationTime = bi.LastAccessTime = bi.LastWriteTime = bi.ChangeTime = 0;
	bi.FileAttributes = 0;
	NtQuerySystemTime(&now);
	if (!ts) { bi.LastAccessTime = bi.LastWriteTime = now; }
	else {
		if (ts[0].tv_nsec == UTIME_NOW) bi.LastAccessTime = now;
		else if (ts[0].tv_nsec != UTIME_OMIT) bi.LastAccessTime = __unix_to_nt(ts[0].tv_sec, ts[0].tv_nsec);
		if (ts[1].tv_nsec == UTIME_NOW) bi.LastWriteTime = now;
		else if (ts[1].tv_nsec != UTIME_OMIT) bi.LastWriteTime = __unix_to_nt(ts[1].tv_sec, ts[1].tv_nsec);
	}
	st = NtSetInformationFile(h, &io, &bi, sizeof bi, FileBasicInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int futimens(int fd, const struct timespec ts[2])
{
	struct __fd *f = __fd_get(fd);
	if (!f) return -1;
	return set_times_handle(f->h, ts);
}

int utimensat(int dirfd, const char *path, const struct timespec ts[2], int flags)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;
	ULONG options;
	int r;
	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	options = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT | (flags & AT_SYMLINK_NOFOLLOW ? FILE_OPEN_REPARSE_POINT : 0);
	st = NtOpenFile(&h, FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS, options);
	if (st == STATUS_ACCESS_DENIED) {
		/* utime.html DESCRIPTION: only write permission on the file
		 * OR ownership is required, never "the file's own mode
		 * forbids it" -- but Wine's server denies a
		 * FILE_WRITE_ATTRIBUTES open outright when the file already
		 * carries FILE_ATTRIBUTE_READONLY (real NT does not; see
		 * src/stat/chmod.c's fchmodat(), which hits the identical
		 * quirk and documents it against
		 * test/posix-unistd.c's test_open_umask_bug()).  Fall back
		 * to a read-attributes-only handle: Wine's
		 * NtSetInformationFile does not itself require
		 * FILE_WRITE_ATTRIBUTES on the handle. */
		st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS, options);
	}
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	r = set_times_handle(h, ts);
	NtClose(h);
	return r;
}

int utimes(const char *path, const struct timeval tv[2])
{
	struct timespec ts[2];
	if (!tv) return utimensat(AT_FDCWD, path, 0, 0);
	ts[0].tv_sec = tv[0].tv_sec; ts[0].tv_nsec = tv[0].tv_usec * 1000;
	ts[1].tv_sec = tv[1].tv_sec; ts[1].tv_nsec = tv[1].tv_usec * 1000;
	return utimensat(AT_FDCWD, path, ts, 0);
}

int utime(const char *path, const struct utimbuf *ub)
{
	struct timespec ts[2];
	if (!ub) return utimensat(AT_FDCWD, path, 0, 0);
	ts[0].tv_sec = ub->actime; ts[0].tv_nsec = 0;
	ts[1].tv_sec = ub->modtime; ts[1].tv_nsec = 0;
	return utimensat(AT_FDCWD, path, ts, 0);
}

int futimes(int fd, const struct timeval tv[2])
{
	struct timespec ts[2];
	if (!tv) return futimens(fd, 0);
	ts[0].tv_sec = tv[0].tv_sec; ts[0].tv_nsec = tv[0].tv_usec * 1000;
	ts[1].tv_sec = tv[1].tv_sec; ts[1].tv_nsec = tv[1].tv_usec * 1000;
	return futimens(fd, ts);
}
int lutimes(const char *path, const struct timeval tv[2])
{
	struct timespec ts[2];
	if (!tv) return utimensat(AT_FDCWD, path, 0, AT_SYMLINK_NOFOLLOW);
	ts[0].tv_sec = tv[0].tv_sec; ts[0].tv_nsec = tv[0].tv_usec * 1000;
	ts[1].tv_sec = tv[1].tv_sec; ts[1].tv_nsec = tv[1].tv_usec * 1000;
	return utimensat(AT_FDCWD, path, ts, AT_SYMLINK_NOFOLLOW);
}

int futimesat(int dirfd, const char *path, const struct timeval tv[2])
{
	struct timespec ts[2];
	if (!tv) return utimensat(dirfd, path, 0, 0);
	ts[0].tv_sec = tv[0].tv_sec; ts[0].tv_nsec = tv[0].tv_usec * 1000;
	ts[1].tv_sec = tv[1].tv_sec; ts[1].tv_nsec = tv[1].tv_usec * 1000;
	return utimensat(dirfd, path, ts, 0);
}
