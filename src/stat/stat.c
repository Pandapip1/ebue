/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * stat, from the information NT keeps.
 *
 * Permission and special bits come from WSL's $LXMOD NTFS extended attribute
 * when it exists.  It is a four-byte mode value and gives files created by
 * ntlibc a persistent POSIX mode without editing a Windows DACL.  ntlibc does
 * not create $LXUID or $LXGID: those are literal IDs in a WSL distribution
 * and cannot be derived from ntlibc's Windows process identity.
 *
 * A file with no $LXMOD keeps a compatibility default for pre-existing
 * Windows files: directories are 0755, ordinary files 0644, and an actual
 * executable PE32/PE32+ image receives 0111 regardless of its suffix.
 * FILE_ATTRIBUTE_READONLY removes 0222.  A suffix alone never grants execute
 * permission, and explicit $LXMOD metadata always wins.
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

static unsigned getle16(const unsigned char *p)
{
	return (unsigned)p[0] | (unsigned)p[1] << 8;
}

static unsigned long getle32(const unsigned char *p)
{
	return (unsigned long)p[0] | (unsigned long)p[1] << 8 |
	       (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

static int read_at(HANDLE h, void *buffer, unsigned length, long long offset)
{
	IO_STATUS_BLOCK io;
	LARGE_INTEGER position = offset;
	NTSTATUS status;

	io.Information = 0;
	status = NtReadFile(h, 0, 0, 0, &io, buffer, length, &position, 0);
	if (status == STATUS_PENDING) {
		NtWaitForSingleObject(h, 0, 0);
		status = io.Status;
	}
	return NT_SUCCESS(status) && io.Information == length;
}

/* Validate enough of the on-disk PE header to distinguish an executable
 * image from a DOS file, DLL, text file with an executable-looking suffix,
 * or an arbitrary file beginning with MZ.  Both PE32 and PE32+ are Windows
 * executable formats; IMAGE_FILE_EXECUTABLE_IMAGE must be set and the DLL
 * characteristic must be clear. */
static int pe_executable(HANDLE h, long long size)
{
	unsigned char dos[64], nt[26];
	unsigned long peoff;
	unsigned characteristics, optional_size, optional_magic;
	long long saved;
	int result = 0;

	if (size < (long long)sizeof dos || __fd_pos_save(h, &saved) < 0)
		return 0;
	if (!read_at(h, dos, sizeof dos, 0) || dos[0] != 'M' || dos[1] != 'Z')
		goto done;
	peoff = getle32(dos + 0x3c);
	if ((long long)peoff > size - (long long)sizeof nt ||
	    !read_at(h, nt, sizeof nt, peoff))
		goto done;
	if (nt[0] != 'P' || nt[1] != 'E' || nt[2] || nt[3]) goto done;
	optional_size = getle16(nt + 20);
	characteristics = getle16(nt + 22);
	optional_magic = getle16(nt + 24);
	if (optional_size >= 2 &&
	    (optional_magic == 0x10b || optional_magic == 0x20b) &&
	    (characteristics & 0x0002) && !(characteristics & 0x2000))
		result = 1;
done:
	__fd_pos_restore(h, saved);
	return result;
}

static mode_t mode_from_attrs(ULONG attrs, ULONG tag, int exe,
                              int have_lxmod, unsigned lxmod)
{
	mode_t m;
	if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) &&
	    (tag == IO_REPARSE_TAG_SYMLINK || tag == IO_REPARSE_TAG_MOUNT_POINT || tag == IO_REPARSE_TAG_LX_SYMLINK))
		m = S_IFLNK;
	else if (attrs & FILE_ATTRIBUTE_DIRECTORY) m = S_IFDIR;
	else if (attrs & FILE_ATTRIBUTE_DEVICE) m = S_IFCHR;
	else m = S_IFREG;
	if (S_ISLNK(m)) m |= 0777;
	else if (S_ISDIR(m)) m |= 0755;
	else if (S_ISCHR(m)) m |= 0666;
	else m |= exe ? 0755 : 0644;
	/* $LXMOD is the POSIX mode record, not merely an execute-bit sidecar.
	 * Keep the type derived from the live NT object, but report every
	 * permission and special bit from the metadata.  Files without the EA
	 * use the validated-PE compatibility fallback. */
	if (have_lxmod) m = (m & S_IFMT) | (lxmod & 07777);
	else if (attrs & FILE_ATTRIBUTE_READONLY) m &= ~0222;
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
	unsigned lxmod = 0;
	int have_lxmod, exe = 0;

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
	have_lxmod = __lxmod_get(h, &lxmod);

	if (!have_lxmod && !(bi.FileAttributes &
	    (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_DEVICE))) {
		int saved = errno;
		exe = pe_executable(h, si.EndOfFile);
		errno = saved;
	}

	st->st_ino = (ino_t)ii.IndexNumber;
	st->st_mode = mode_from_attrs(bi.FileAttributes, ti.ReparseTag,
	                              exe, have_lxmod, lxmod);
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
	int result;
	if (!f) return -1;
	if (f->vfs && !f->vfs_native) return __vfs_stat(f->vfs, st);
	result = __fstat_handle(f->h, f->type, st);
	/* Wine does not retain the $LXMOD EA supplied to NtCreateFile.  A
	 * shm descriptor carries the mode read from its private namespace
	 * sidecar so fstat() can report the same persistent POSIX metadata. */
	if (result == 0 && f->shm_mode_valid)
		st->st_mode = (st->st_mode & S_IFMT) | (f->shm_mode & 07777);
	return result;
}

int fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS s;
	int r, vfs;

	if (!path) { errno = EFAULT; return -1; }
	if (!strncmp(path, "/dev/", 5)) {
		int fd = -1;
		if (!strcmp(path, "/dev/stdin")) fd = 0;
		else if (!strcmp(path, "/dev/stdout")) fd = 1;
		else if (!strcmp(path, "/dev/stderr")) fd = 2;
		if (fd >= 0) return fstat(fd, st);
	}
	vfs = __vfs_resolve_at(dirfd, path);
	if (vfs < 0) return -1;
	if (vfs & __VFS_NATIVE) vfs = __VFS_NONE;
	if (vfs == __VFS_MISSING) { errno = ENOENT; return -1; }
	if (vfs != __VFS_NONE) return __vfs_stat(vfs, st);

	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	s = NtOpenFile(&h, FILE_READ_DATA | FILE_READ_ATTRIBUTES | FILE_READ_EA |
	               SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS,
	               FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT |
	               (flags & AT_SYMLINK_NOFOLLOW ? FILE_OPEN_REPARSE_POINT : 0));
	/* stat() must not require permission to read file data.  Reading is only
	 * needed for the metadata-free PE default, so retain the ordinary
	 * attribute-only result when that extra access is denied. */
	if (!NT_SUCCESS(s) && s != STATUS_IO_REPARSE_TAG_NOT_HANDLED)
		s = NtOpenFile(&h, FILE_READ_ATTRIBUTES | FILE_READ_EA | SYNCHRONIZE,
		               &np.oa, &io, FILE_SHARE_VALID_FLAGS,
		               FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT |
		               (flags & AT_SYMLINK_NOFOLLOW ? FILE_OPEN_REPARSE_POINT : 0));
	if (s == STATUS_IO_REPARSE_TAG_NOT_HANDLED && !(flags & AT_SYMLINK_NOFOLLOW)) {
		/* A reparse point of a kind nothing resolves (WSL links): report it as is. */
		s = NtOpenFile(&h, FILE_READ_DATA | FILE_READ_ATTRIBUTES | FILE_READ_EA |
		               SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS,
		               FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT | FILE_OPEN_REPARSE_POINT);
		if (!NT_SUCCESS(s))
			s = NtOpenFile(&h, FILE_READ_ATTRIBUTES | FILE_READ_EA | SYNCHRONIZE,
			               &np.oa, &io, FILE_SHARE_VALID_FLAGS,
			               FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT |
			               FILE_OPEN_REPARSE_POINT);
	}
	__ntpath_free(&np);
	if (!NT_SUCCESS(s)) return __set_errno_status(s);
	r = __fstat_handle(h, __FD_FILE, st);
	NtClose(h);
	return r;
}

int stat(const char *path, struct stat *st) { return fstatat(AT_FDCWD, path, st, 0); }
int lstat(const char *path, struct stat *st) { return fstatat(AT_FDCWD, path, st, AT_SYMLINK_NOFOLLOW); }
