/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * posix_fadvise() is purely advisory per POSIX, and NT has no per-handle
 * readahead/cache-priority knob, so this validates arguments and
 * no-ops (after refusing a pipe/FIFO, per its ERRORS section).
 *
 * posix_fallocate() is a real implementation: NtSetInformationFile
 * (FileAllocationInformation) genuinely reserves storage on NTFS, and
 * advances EOF if the range extends past it, matching Linux's
 * fallocate() (reserving real clusters rather than leaving a sparse hole).
 */
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include "libc.h"
#include "plat_fcntl.h"

int posix_fadvise(int fd, off_t offset, off_t len, int advice) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct __fd *f = __fd_get(fd);
	/* offset is unconstrained: the range need not exist in the file. */
	(void)offset;
	/* posix_fadvise() returns the error number directly, not -1/errno. */
	if (!f) return EBADF;
	/* len == 0 means "to EOF", not a degenerate length, so only < 0 fails. */
	if (len < 0) return EINVAL;
	switch (advice) {
	case POSIX_FADV_NORMAL: case POSIX_FADV_RANDOM: case POSIX_FADV_SEQUENTIAL:
	case POSIX_FADV_WILLNEED: case POSIX_FADV_DONTNEED: case POSIX_FADV_NOREUSE:
		break;
	default:
		return EINVAL;
	}
	/* __FD_PIPE covers both an anonymous pipe() and a named FIFO (NT has
	 * no separate device type for the two). Checked last, after both
	 * EINVAL halves, matching posix_fallocate()'s ordering below so the
	 * two functions agree on a descriptor that fails two clauses at once. */
	if (f->type == __FD_PIPE) return ESPIPE;
	return 0;
}

/* The maximum file size of the volume a handle lives on, for
 * posix_fallocate()'s [EFBIG].
 *
 * posix_fallocate.html's clause is "[EFBIG] The value of offset+len is
 * greater than the maximum file size" -- a property of the VOLUME, not
 * of the operating system, which is why this is derived per volume
 * rather than being one constant for all of them.
 *
 * NT cannot be asked directly, and cannot be left to answer for itself.
 * Measured on this tree: ftruncate() to 32 TiB, 256 TiB, 8 PiB, 2^61
 * and 2^62 all fail identically with STATUS mapping to ENOMEM, and the
 * threshold tracks the volume's FREE SPACE (1 TiB succeeded), not any
 * file-size limit, so the bound is computed here as cluster_size *
 * (2^32 - 1), matching NTFS's real published limit table (not the
 * misleading "2^64-1" figure in Microsoft's file-system comparison
 * table, which is the on-disk field width, not the supported maximum).
 *
 * A query failure or zero cluster size yields LLONG_MAX (unlimited):
 * guessing too permissive is caught by the kernel refusing the write;
 * guessing too restrictive would refuse a request that could have
 * worked, with nothing to correct it. This makes EFBIG best-effort off
 * NTFS (FAT32's real 4 GiB ceiling isn't described by this formula, but
 * is enforced by NT itself).
 *
 * The query and derivation live in __plat_volume_max_file_size()
 * (src/internal/plat_fcntl.h); this comment stays where the decision is made. */

int posix_fallocate(int fd, off_t offset, off_t len) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct __fd *f = __fd_get(fd);
	long long want, alloc_size, eof;
	int grow_alloc;

	if (!f) return EBADF;
	if (offset < 0 || len < 0) return EINVAL;
	if (f->type == __FD_PIPE) return ESPIPE;
	/* A read-only fd must be refused since this may extend the file;
	 * relies on inherited descriptors recording their real access mode
	 * (src/internal/fd.c's accmode_of), same test as src/unistd/write.c.
	 * Checked after ESPIPE since that clause says the more useful thing
	 * about a pipe's read end, which would satisfy both. */
	if ((f->flags & O_ACCMODE) == O_RDONLY) return EBADF;
	/* __FD_FILE excludes directories, devices, sockets and pipes, matching
	 * ENODEV's "does not refer to a regular file" exactly. */
	if (f->type != __FD_FILE) return ENODEV;
	/* Do not fold into `want = offset + len; if (want < 0) return EFBIG`:
	 * that relies on signed overflow, which is UB and was optimized away
	 * (caught by UBSan). Both operands are non-negative here, so the sum
	 * cannot overflow. The 4 GiB floor (FAT32's ceiling) means the
	 * ordinary path never issues a volume query. */
	if (offset > LLONG_MAX - len) return EFBIG;
	want = (long long)offset + (long long)len;
	if (__fsize_allow(want) < 0) return EFBIG;
	if (want > 4LL * 1024 * 1024 * 1024 && want > __plat_volume_max_file_size(f->h))
		return EFBIG;

	if (__plat_file_extent(f->h, &alloc_size, &eof) < 0) return errno;

	/* AllocationSize and EndOfFile must be compared separately, not just
	 * against `want` — see __plat_fallocate() (src/fcntl/nt/plat_fcntl.c)
	 * for the sparse-file/Wine-hole cases this guards against. */
	grow_alloc = want > alloc_size && want >= eof;
	return __plat_fallocate(f->h, want, eof, grow_alloc);
}
