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
	LARGE_INTEGER converted[2] = { 0, 0 };
	NTSTATUS st;
	int i;

	if (ts) {
		for (i = 0; i < 2; i++) {
			if (ts[i].tv_nsec == UTIME_NOW || ts[i].tv_nsec == UTIME_OMIT)
				continue;
			if (ts[i].tv_nsec < 0 || ts[i].tv_nsec >= 1000000000L) {
				errno = EINVAL;
				return -1;
			}
			if (!__unix_to_nt(ts[i].tv_sec, ts[i].tv_nsec,
			    &converted[i])) {
				errno = EOVERFLOW;
				return -1;
			}
		}
	}

	/* utime.html DESCRIPTION says only the access/modification times
	 * change -- the mode must survive untouched.  FILE_BASIC_INFORMATION
	 * documents FileAttributes==0 as "leave the attributes alone", and
	 * real NT does honor that -- but stock Wine (the Wine CI actually
	 * runs) does NOT: its NtSetInformationFile silently clears
	 * FILE_ATTRIBUTE_READONLY on every timestamp-only call regardless of
	 * what FileAttributes says.  We carry a local, unpushed Wine patch
	 * that fixes this, but that patch exists only on this machine -- it
	 * is not upstream and it is not what CI installs from apt.  "We
	 * fixed it in our Wine" is therefore never sufficient justification
	 * for relying on FileAttributes==0 here; test against an unpatched
	 * Wine, not just the local one, before ever touching this again.  So
	 * always query the current attributes and pass them back explicitly,
	 * on every Wine and on real NT alike.  This round-trip needs
	 * FILE_READ_ATTRIBUTES on the handle, which utimensat()'s primary
	 * open below requests specifically for this; see the comment there. */
	st = NtQueryInformationFile(h, &io, &bi, sizeof bi, FileBasicInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	bi.CreationTime = bi.LastAccessTime = bi.LastWriteTime = bi.ChangeTime = 0;
	NtQuerySystemTime(&now);
	if (!ts) { bi.LastAccessTime = bi.LastWriteTime = now; }
	else {
		if (ts[0].tv_nsec == UTIME_NOW) bi.LastAccessTime = now;
		else if (ts[0].tv_nsec != UTIME_OMIT) bi.LastAccessTime = converted[0];
		if (ts[1].tv_nsec == UTIME_NOW) bi.LastWriteTime = now;
		else if (ts[1].tv_nsec != UTIME_OMIT) bi.LastWriteTime = converted[1];
	}
	st = NtSetInformationFile(h, &io, &bi, sizeof bi, FileBasicInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int futimens(int fd, const struct timespec ts[2])
{
	struct __fd *f = __fd_get(fd);
	if (!f) return -1;
	if (f->vfs && !f->vfs_native) { errno = EROFS; return -1; }
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
	/* FILE_READ_ATTRIBUTES is requested alongside FILE_WRITE_ATTRIBUTES
	 * because set_times_handle() below round-trips the current
	 * attributes through NtQueryInformationFile before writing them
	 * back (see the comment there for why that round-trip must stay).
	 * NtQueryInformationFile(FileBasicInformation) requires
	 * FILE_READ_ATTRIBUTES on real NT -- Wine doesn't enforce that
	 * check, but real NT does, and omitting it here is exactly what
	 * turned every ordinary utimensat() into STATUS_ACCESS_DENIED on
	 * real Windows before. */
	st = NtOpenFile(&h, FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS, options);
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
		 * FILE_WRITE_ATTRIBUTES on the handle, and this path is only
		 * ever reached on Wine in the first place -- real NT never
		 * denies the FILE_WRITE_ATTRIBUTES open above on a read-only
		 * file, so real NT never falls back to this handle.  The
		 * fallback handle keeps FILE_READ_ATTRIBUTES, which is all
		 * set_times_handle()'s query needs; it does not need
		 * FILE_WRITE_ATTRIBUTES again because that's precisely the
		 * access Wine already told us the file cannot grant. */
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
	if (tv[0].tv_usec < 0 || tv[0].tv_usec >= 1000000 ||
	    tv[1].tv_usec < 0 || tv[1].tv_usec >= 1000000) {
		errno = EINVAL;
		return -1;
	}
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

int futimesat(int dirfd, const char *path, const struct timeval tv[2])
{
	struct timespec ts[2];
	if (!tv) return utimensat(dirfd, path, 0, 0);
	if (tv[0].tv_usec < 0 || tv[0].tv_usec >= 1000000 ||
	    tv[1].tv_usec < 0 || tv[1].tv_usec >= 1000000) {
		errno = EINVAL;
		return -1;
	}
	ts[0].tv_sec = tv[0].tv_sec; ts[0].tv_nsec = tv[0].tv_usec * 1000;
	ts[1].tv_sec = tv[1].tv_sec; ts[1].tv_nsec = tv[1].tv_usec * 1000;
	return utimensat(dirfd, path, ts, 0);
}
