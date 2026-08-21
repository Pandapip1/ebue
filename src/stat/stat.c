/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * stat, from the information NT keeps.
 *
 * Mode bits are made up the way every Unix layer on Windows makes them
 * up: a directory is 0755, a file 0644 (0444 when read-only), and a file
 * whose name ends in .exe, .com, .bat or .cmd gets the execute bits --
 * as does any file starting with "#!" or "MZ" when that is cheap to
 * check, which it is not here, so only the name is consulted.  The inode
 * is the NTFS file reference number, the device the volume serial.
 */
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

static int has_exe_suffix(const WCHAR *name, size_t n)
{
	static const char *const sfx[] = { ".exe", ".com", ".bat", ".cmd", ".sh", 0 };
	int i;
	for (i = 0; sfx[i]; i++) {
		size_t l = strlen(sfx[i]), k;
		if (n < l) continue;
		for (k = 0; k < l; k++) {
			WCHAR c = name[n - l + k];
			if (c >= 'A' && c <= 'Z') c += 32;
			if (c != (unsigned char)sfx[i][k]) break;
		}
		if (k == l) return 1;
	}
	return 0;
}

static mode_t mode_from_attrs(ULONG attrs, ULONG tag, int exe)
{
	mode_t m;
	if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) &&
	    (tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT || tag == IO_REPARSE_TAG_LX_SYMLINK))
		return S_IFLNK | 0777;
	if (attrs & FILE_ATTRIBUTE_DIRECTORY) m = S_IFDIR | 0755;
	else if (attrs & FILE_ATTRIBUTE_DEVICE) m = S_IFCHR | 0666;
	else m = S_IFREG | (exe ? 0755 : 0644);
	if (attrs & FILE_ATTRIBUTE_READONLY) m &= ~0222;
	return m;
}

int __fstat_handle(HANDLE h, int type, struct stat *st)
{
	IO_STATUS_BLOCK io;
	FILE_BASIC_INFORMATION bi;
	FILE_STANDARD_INFORMATION si;
	FILE_INTERNAL_INFORMATION ii;
	FILE_ATTRIBUTE_TAG_INFORMATION ti;
	FILE_FS_VOLUME_INFORMATION vi;
	NTSTATUS s;
	int exe = 0;

	memset(st, 0, sizeof *st);
	if (type == __FD_PIPE) { st->st_mode = S_IFIFO | 0600; st->st_nlink = 1; st->st_blksize = 4096; return 0; }
	if (type == __FD_CONSOLE || type == __FD_CHAR || type == __FD_UNKNOWN) { st->st_mode = S_IFCHR | 0600; st->st_nlink = 1; st->st_blksize = 4096; return 0; }

	s = NtQueryInformationFile(h, &io, &bi, sizeof bi, FileBasicInformation);
	if (!NT_SUCCESS(s)) return __set_errno_status(s);
	s = NtQueryInformationFile(h, &io, &si, sizeof si, FileStandardInformation);
	if (!NT_SUCCESS(s)) return __set_errno_status(s);
	if (!NT_SUCCESS(NtQueryInformationFile(h, &io, &ii, sizeof ii, FileInternalInformation))) ii.IndexNumber = 0;
	if (!NT_SUCCESS(NtQueryInformationFile(h, &io, &ti, sizeof ti, FileAttributeTagInformation))) ti.ReparseTag = 0;
	if (NT_SUCCESS(NtQueryVolumeInformationFile(h, &io, &vi, sizeof vi, FileFsVolumeInformation)) ||
	    io.Information >= offsetof(FILE_FS_VOLUME_INFORMATION, VolumeLabel))
		st->st_dev = vi.VolumeSerialNumber;

	if (!(bi.FileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
		char nb[sizeof(FILE_NAME_INFORMATION) + 520];
		FILE_NAME_INFORMATION *ni = (FILE_NAME_INFORMATION *)nb;
		s = NtQueryInformationFile(h, &io, ni, sizeof nb, FileNameInformation);
		if (NT_SUCCESS(s) || s == STATUS_BUFFER_OVERFLOW) {
			size_t n = ni->FileNameLength / sizeof(WCHAR);
			if (n > 260) n = 260;
			exe = has_exe_suffix(ni->FileName, n);
		}
	}

	st->st_ino = (ino_t)ii.IndexNumber;
	st->st_mode = mode_from_attrs(bi.FileAttributes, ti.ReparseTag, exe);
	st->st_nlink = si.NumberOfLinks ? si.NumberOfLinks : 1;
	st->st_uid = 1000;
	st->st_gid = 1000;
	st->st_size = S_ISDIR(st->st_mode) ? 0 : si.EndOfFile;
	st->st_blksize = 4096;
	st->st_blocks = (si.AllocationSize + 511) / 512;
	st->st_atim.tv_sec = __nt_to_unix_sec(bi.LastAccessTime);
	st->st_atim.tv_nsec = __nt_to_unix_nsec(bi.LastAccessTime);
	st->st_mtim.tv_sec = __nt_to_unix_sec(bi.LastWriteTime);
	st->st_mtim.tv_nsec = __nt_to_unix_nsec(bi.LastWriteTime);
	st->st_ctim.tv_sec = __nt_to_unix_sec(bi.ChangeTime);
	st->st_ctim.tv_nsec = __nt_to_unix_nsec(bi.ChangeTime);
	return 0;
}

int fstat(int fd, struct stat *st)
{
	struct __fd *f = __fd_get(fd);
	if (!f) return -1;
	return __fstat_handle(f->h, f->type, st);
}

int fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS s;
	int r;

	if (!strncmp(path, "/dev/", 5)) {
		int fd = -1;
		if (!strcmp(path, "/dev/null") || !strcmp(path, "/dev/tty")) {
			memset(st, 0, sizeof *st);
			st->st_mode = S_IFCHR | 0666;
			st->st_nlink = 1;
			return 0;
		}
		if (!strcmp(path, "/dev/stdin")) fd = 0;
		else if (!strcmp(path, "/dev/stdout")) fd = 1;
		else if (!strcmp(path, "/dev/stderr")) fd = 2;
		if (fd >= 0) return fstat(fd, st);
	}

	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	s = NtOpenFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS,
	               FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT |
	               (flags & AT_SYMLINK_NOFOLLOW ? FILE_OPEN_REPARSE_POINT : 0));
	if (s == STATUS_IO_REPARSE_TAG_NOT_HANDLED && !(flags & AT_SYMLINK_NOFOLLOW)) {
		/* A reparse point of a kind nothing resolves (WSL links): report it as is. */
		s = NtOpenFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS,
		               FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REPARSE_POINT);
	}
	__ntpath_free(&np);
	if (!NT_SUCCESS(s)) return __set_errno_status(s);
	r = __fstat_handle(h, __FD_FILE, st);
	NtClose(h);
	return r;
}

int stat(const char *path, struct stat *st) { return fstatat(AT_FDCWD, path, st, 0); }
int lstat(const char *path, struct stat *st) { return fstatat(AT_FDCWD, path, st, AT_SYMLINK_NOFOLLOW); }
