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
	int r;
	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	st = NtOpenFile(&h, FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS,
	                FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT | (flags & AT_SYMLINK_NOFOLLOW ? FILE_OPEN_REPARSE_POINT : 0));
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
