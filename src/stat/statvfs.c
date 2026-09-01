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

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "libc.h"
#include "plat_stat.h"
#include "ownership_stubs.h"

int fstatvfs(int fd, struct statvfs *buf)
{
	struct __fd *f = __fd_get(fd);
	if (!f) return -1;   /* __fd_get sets EBADF */
	if (!buf) { errno = EFAULT; return -1; }
	if (f->vfs && !f->vfs_native) {
		__ownership_writable_span(buf, sizeof *buf);
		memset(buf, 0, sizeof *buf);
		buf->f_bsize = buf->f_frsize = 4096;
		buf->f_fsid = 0xffffffffu;
		buf->f_flag = ST_RDONLY | ST_NOSUID;
		buf->f_namemax = 255;
		return 0;
	}
	return __plat_statvfs(f->h, buf);
}

int statvfs(const char *__restrict path, struct statvfs *__restrict buf)
{
	if (!buf) { errno = EFAULT; return -1; }
	return __plat_statvfs_path(path, buf);
}

// NOLINTEND(misc-include-cleaner)
