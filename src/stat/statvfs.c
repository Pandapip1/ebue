/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * statvfs/fstatvfs --
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/fstatvfs.html
 *
 * Everything here comes from NtQueryVolumeInformationFile, which is
 * pure NTDLL and works on any open handle on the volume -- so both
 * entry points share one filler and differ only in how they get a
 * handle, exactly as stat()/fstat() do in stat.c next door.
 *
 * Field by field, with the NT class each value is derived from.  The
 * spec's own escape clause covers the two that have no source at all:
 * "It is unspecified whether all members of the statvfs structure have
 * meaningful values on all file systems" (DESCRIPTION).
 *
 *   f_bsize, f_frsize
 *      SectorsPerAllocationUnit * BytesPerSector -- the cluster size.
 *      NT's allocation unit *is* the fundamental block: every size NT
 *      reports for the volume is counted in them, so f_frsize and
 *      f_bsize are the same number here.  They are allowed to differ
 *      (f_bsize is a preferred I/O size, f_frsize the unit f_blocks is
 *      counted in) but on NT there is nothing to make them differ.
 *
 *   f_blocks, f_bfree, f_bavail
 *      FileFsFullSizeInformation's TotalAllocationUnits,
 *      ActualAvailableAllocationUnits and
 *      CallerAvailableAllocationUnits.  That class exists precisely to
 *      separate "free on the volume" from "free to this caller after
 *      quota", which is POSIX's f_bfree/f_bavail split exactly.  When
 *      the volume does not support it, FileFsSizeInformation is the
 *      fallback and reports only the caller-available figure -- so
 *      f_bfree is then set equal to f_bavail rather than invented.
 *
 *   f_files, f_ffree, f_favail
 *      **Always 0.  NT has no file-serial-number pool to report.**  A
 *      POSIX file system allocates inodes out of a fixed table and can
 *      say how many are left; NTFS grows its MFT on demand and none of
 *      the FileFs* classes exposes a record count, free or total.  Any
 *      nonzero number here would be fabricated, and (unlike a zero,
 *      which the DESCRIPTION clause above covers) would be believed by
 *      a caller doing capacity arithmetic.  Zero is the honest answer,
 *      not a placeholder to be filled in later.
 *
 *   f_fsid
 *      FileFsVolumeInformation's VolumeSerialNumber -- the same value
 *      stat.c uses for st_dev, so the two agree about what "the same
 *      file system" means, which is the only property POSIX gives
 *      f_fsid.
 *
 *   f_namemax
 *      FileFsAttributeInformation's MaximumComponentNameLength (255 on
 *      NTFS), in characters.
 *
 *   f_flag
 *      ST_RDONLY comes from either FILE_READ_ONLY_VOLUME in
 *      FileSystemAttributes (a read-only mount) or FILE_READ_ONLY_DEVICE
 *      in FileFsDeviceInformation's Characteristics (read-only media --
 *      a CD-ROM is read-only without the file system saying so).  Both
 *      are checked because they are genuinely different conditions.
 *
 *      ST_NOSUID is set unconditionally, and that is a real mapping
 *      rather than a default: basedefs/sys_statvfs.h.html defines it as
 *      "does not support the semantics of the ST_ISUID and ST_ISGID
 *      file mode bits", and no NT file system does.  ntlibc never
 *      produces those bits from stat() (see stat.c's mode_from_attrs,
 *      which synthesises 0755/0644/0444 and the execute bits and
 *      nothing else) and its exec() family never honours them.  So the
 *      bit is *true here*, on every volume, and omitting it would be
 *      the inaccurate choice.
 */
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

static int statvfs_handle(HANDLE h, struct statvfs *buf)
{
	IO_STATUS_BLOCK io;
	FILE_FS_FULL_SIZE_INFORMATION fsi;
	FILE_FS_SIZE_INFORMATION si;
	FILE_FS_DEVICE_INFORMATION di;
	/* FileSystemName/VolumeLabel are variable-length; over-allocate so
	 * the fixed head is never truncated by a long name.  Only the
	 * fixed members are read. */
	union { FILE_FS_ATTRIBUTE_INFORMATION a; char pad[sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + 256 * sizeof(WCHAR)]; } ab;
	union { FILE_FS_VOLUME_INFORMATION v; char pad[sizeof(FILE_FS_VOLUME_INFORMATION) + 256 * sizeof(WCHAR)]; } vb;
	NTSTATUS s;
	unsigned long long cluster;

	memset(buf, 0, sizeof *buf);

	s = NtQueryVolumeInformationFile(h, &io, &fsi, sizeof fsi, FileFsFullSizeInformation);
	if (NT_SUCCESS(s)) {
		cluster = (unsigned long long)fsi.SectorsPerAllocationUnit * fsi.BytesPerSector;
		buf->f_blocks = (fsblkcnt_t)fsi.TotalAllocationUnits;
		buf->f_bfree = (fsblkcnt_t)fsi.ActualAvailableAllocationUnits;
		buf->f_bavail = (fsblkcnt_t)fsi.CallerAvailableAllocationUnits;
	} else {
		s = NtQueryVolumeInformationFile(h, &io, &si, sizeof si, FileFsSizeInformation);
		if (!NT_SUCCESS(s)) return __set_errno_status(s);
		cluster = (unsigned long long)si.SectorsPerAllocationUnit * si.BytesPerSector;
		buf->f_blocks = (fsblkcnt_t)si.TotalAllocationUnits;
		/* This class reports only the caller-visible free count; with
		 * no second figure to distinguish them, f_bfree is that same
		 * number rather than a guess at what quota is hiding. */
		buf->f_bavail = buf->f_bfree = (fsblkcnt_t)si.AvailableAllocationUnits;
	}

	/* fstatvfs.html ERRORS: [EOVERFLOW] "One of the values to be
	 * returned cannot be represented correctly in the structure
	 * pointed to by buf."  f_bsize/f_frsize are `unsigned long`, which
	 * is 32-bit under this target's LLP64 model on both arches, while
	 * the cluster size is computed from two ULONGs and could in
	 * principle exceed it.  The block *counts* cannot overflow:
	 * fsblkcnt_t is unsigned 64-bit and the NT counters are signed
	 * 64-bit LARGE_INTEGERs. */
	if (cluster > (unsigned long)-1) { errno = EOVERFLOW; return -1; }
	buf->f_bsize = buf->f_frsize = (unsigned long)cluster;

	/* f_files/f_ffree/f_favail stay 0 from the memset above -- see the
	 * banner.  NT exposes no file-serial-number pool. */

	buf->f_flag = ST_NOSUID;

	s = NtQueryVolumeInformationFile(h, &io, &ab, sizeof ab, FileFsAttributeInformation);
	if (NT_SUCCESS(s)) {
		if (ab.a.FileSystemAttributes & FILE_READ_ONLY_VOLUME) buf->f_flag |= ST_RDONLY;
		if (ab.a.MaximumComponentNameLength > 0) buf->f_namemax = (unsigned long)ab.a.MaximumComponentNameLength;
	}

	s = NtQueryVolumeInformationFile(h, &io, &di, sizeof di, FileFsDeviceInformation);
	if (NT_SUCCESS(s) && (di.Characteristics & FILE_READ_ONLY_DEVICE)) buf->f_flag |= ST_RDONLY;

	s = NtQueryVolumeInformationFile(h, &io, &vb, sizeof vb, FileFsVolumeInformation);
	if (NT_SUCCESS(s)) buf->f_fsid = vb.v.VolumeSerialNumber;

	return 0;
}

int fstatvfs(int fd, struct statvfs *buf)
{
	struct __fd *f = __fd_get(fd);
	if (!f) return -1;   /* __fd_get sets EBADF */
	if (!buf) { errno = EFAULT; return -1; }
	return statvfs_handle(f->h, buf);
}

int statvfs(const char *__restrict path, struct statvfs *__restrict buf)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS s;
	int r;

	if (!buf) { errno = EFAULT; return -1; }
	/* "Read, write, or execute permission of the named file is not
	 * required" (DESCRIPTION) -- FILE_READ_ATTRIBUTES is the NT access
	 * mask that asks for none of the three. */
	if (__ntpath(path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	s = NtOpenFile(&h, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &np.oa, &io, FILE_SHARE_VALID_FLAGS,
	               FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);
	__ntpath_free(&np);
	if (!NT_SUCCESS(s)) return __set_errno_status(s);
	r = statvfs_handle(h, buf);
	NtClose(h);
	return r;
}
