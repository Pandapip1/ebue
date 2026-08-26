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
 *
 * Pipes, consoles, character devices and the "couldn't classify it"
 * fallback get a synthetic st_dev/st_ino instead: stat.html's DESCRIPTION
 * requires "[st_ino] together with [st_dev] uniquely identify the file
 * within the system" (see also <sys/stat.h>'s own text to that effect),
 * and the universal same-file idiom (`a.st_dev==b.st_dev &&
 * a.st_ino==b.st_ino`) depends on it -- see __fstat_synthetic_ino below
 * for how that identity is derived and __STAT_DEV_PIPE/__STAT_DEV_CHAR
 * for why the device half of it can never collide with a real volume
 * serial number.
 */
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

/* A real st_dev (below) is always vi.VolumeSerialNumber, a plain ULONG
 * assigned straight into the 64-bit dev_t -- so its top 32 bits are
 * always zero.  Setting all of ours gives values no real volume serial
 * number can ever equal, while still keeping __STAT_DEV_PIPE and
 * __STAT_DEV_CHAR distinct from each other -- so a pipe can never be
 * mistaken for a console/char device, or either for a real file. */
#define __STAT_DEV_PIPE ((dev_t)0xFFFFFFFF00000001ULL)
#define __STAT_DEV_CHAR ((dev_t)0xFFFFFFFF00000002ULL)

/* FNV-1a, a well-known 64-bit hash (offset basis and prime from the
 * canonical spec, http://www.isthe.com/chongo/tech/comp/fnv/), used
 * below to fold a variable-length NT object name into a fixed 64-bit
 * st_ino. */
static ino_t fnv1a64(const void *data, size_t n)
{
	const unsigned char *p = data;
	ino_t h = 0xcbf29ce484222325ULL;
	size_t i;
	for (i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ULL; }
	return h;
}

/* The identity source for a pipe/console/char/unknown handle, in order
 * of preference -- each candidate was checked, not assumed (see the
 * commit message for the empirical results and citations):
 *
 * 1. FileInternalInformation, the same NTFS-style file reference number
 *    the regular-file path below uses.  NPFS (the named-pipe file
 *    system) does not support it -- confirmed both under Wine (it
 *    answers STATUS_NOT_IMPLEMENTED) and by
 *    <https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileinformationbyhandle>,
 *    whose kernel32 GetFileInformationByHandle is built from this same
 *    query and whose docs say outright "This handle should not be a
 *    pipe handle" -- but a real ConDrv console handle answers it with a
 *    real, distinct-per-handle value (confirmed under Wine: stdin and
 *    stdout come back with two different nonzero IndexNumbers), so it
 *    is tried first and used whenever it succeeds with a nonzero value.
 *
 * 2. NtQueryObject's ObjectNameInformation, hashed.  A console handle
 *    has no name (confirmed under Wine: empty every time), but a pipe
 *    does -- and, for this library's own pipe2() (src/unistd/pipe.c),
 *    the read and write ends of the *same* pipe are opens of the *same*
 *    NT path, so they hash to the same st_ino while a second pipe2()
 *    call (a different path) hashes to a different one.  That means
 *    stat()/fstat() on the two ends of one pipe report it as "the same
 *    file" by the st_dev/st_ino test -- defensible, since they are two
 *    handles to the same underlying NPFS file object, and the
 *    alternative (handle value) would make even the *same* end of the
 *    same pipe stop matching itself across dup() (see 3 below), which
 *    is the worse failure mode for the callers this matters to (see
 *    same_file()-shaped code, e.g. GNU diffutils).  An inherited or
 *    foreign anonymous pipe not created by pipe2() still has an NT path
 *    (kernel32's CreatePipe names them too) so this still applies to
 *    handles this library did not create itself.
 *
 * 3. The handle value itself, when neither of the above produced
 *    anything: unique within this process and stable for the handle's
 *    own lifetime, but NOT stable across dup() (a dup'd fd is a
 *    different NT handle to the same object) -- so two ends of the
 *    "same" file reached only through this fallback can wrongly compare
 *    as different files.  This only happens when both a
 *    FileInternalInformation query and an object-name query find
 *    nothing to work with, which nothing observed so far exercises. */
static ino_t __fstat_synthetic_ino(HANDLE h)
{
	IO_STATUS_BLOCK io;
	FILE_INTERNAL_INFORMATION ii;
	NTSTATUS s;

	s = NtQueryInformationFile(h, &io, &ii, sizeof ii, FileInternalInformation);
	if (NT_SUCCESS(s) && ii.IndexNumber != 0) return (ino_t)ii.IndexNumber;

	{
		char buf[512] = { 0 };
		ULONG ret = 0;
		OBJECT_NAME_INFORMATION *ni = (OBJECT_NAME_INFORMATION *)buf;
		s = NtQueryObject(h, ObjectNameInformation, buf, sizeof buf, &ret);
		if (NT_SUCCESS(s) && ni->Name.Length > 0)
			return fnv1a64(ni->Name.Buffer, ni->Name.Length);
	}

	return (ino_t)(ULONG_PTR)h;
}

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
	if (type == __FD_PIPE) {
		st->st_dev = __STAT_DEV_PIPE;
		st->st_ino = __fstat_synthetic_ino(h);
		st->st_mode = S_IFIFO | 0600; st->st_nlink = 1; st->st_blksize = 4096; return 0;
	}
	if (type == __FD_CONSOLE || type == __FD_CHAR || type == __FD_UNKNOWN) {
		st->st_dev = __STAT_DEV_CHAR;
		st->st_ino = __fstat_synthetic_ino(h);
		st->st_mode = S_IFCHR | 0600; st->st_nlink = 1; st->st_blksize = 4096; return 0;
	}

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
	st->st_uid = getuid();
	st->st_gid = getgid();
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
