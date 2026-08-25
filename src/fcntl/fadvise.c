/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * posix_fadvise(): POSIX (XBD posix_fadvise) says the function "shall
 * have no effect on the semantics of other operations on the specified
 * data, although it may affect the performance of other operations" --
 * every advice value is optional and purely advisory, so a conforming
 * implementation is free to do nothing but validate its arguments and
 * report success. NT has no per-handle readahead/cache-priority knob
 * this library reaches for elsewhere, so that is exactly what this
 * does: check the fd and the advice value are both valid (the two
 * required error cases: EBADF, EINVAL) and otherwise no-op.
 *
 * posix_fallocate() is a real implementation, not a no-op: POSIX
 * requires it to "ensure that any required storage for regular file
 * data starting at offset and continuing for len bytes is allocated on
 * the file system storage media" -- a guarantee, not a hint -- which
 * NtSetInformationFile(FileAllocationInformation) genuinely provides on
 * NTFS. If the requested range extends past the current end of file,
 * the end of file is also advanced (matching Linux's fallocate()
 * behaviour for the file size, though not the "may leave a sparse hole"
 * wording some systems allow -- FileAllocationInformation reserves real
 * clusters).
 */
#include <fcntl.h>
#include <errno.h>
#include "libc.h"

int posix_fadvise(int fd, off_t offset, off_t len, int advice)
{
	struct __fd *f = __fd_get(fd);
	(void)offset; (void)len;
	/* posix_fadvise() returns the error number directly, not -1/errno. */
	if (!f) return EBADF;
	switch (advice) {
	case POSIX_FADV_NORMAL: case POSIX_FADV_RANDOM: case POSIX_FADV_SEQUENTIAL:
	case POSIX_FADV_WILLNEED: case POSIX_FADV_DONTNEED: case POSIX_FADV_NOREUSE:
		return 0;
	default:
		return EINVAL;
	}
}

int posix_fallocate(int fd, off_t offset, off_t len)
{
	struct __fd *f = __fd_get(fd);
	IO_STATUS_BLOCK io;
	FILE_STANDARD_INFORMATION si;
	FILE_ALLOCATION_INFORMATION ai;
	FILE_END_OF_FILE_INFORMATION eof;
	NTSTATUS st;
	long long want;

	if (!f) return EBADF;
	if (offset < 0 || len < 0) return EINVAL;
	if (f->type == __FD_PIPE) return ESPIPE;
	/* posix_fallocate.html ERRORS, *shall fail*: "[EBADF] The fd argument
	 * references a file that was opened without write permission."  This
	 * function allocates storage and may extend the file, so a descriptor
	 * the caller cannot write through must be refused -- previously it
	 * was not checked at all, and posix_fallocate(rd, 0, 5) on a
	 * read-only descriptor with the range already inside the file
	 * returned 0, promising an allocation the caller can never use.
	 * (A range PAST the end happened to fail as well, but only as a side
	 * effect of NT refusing the FileEndOfFileInformation set on a
	 * read-only handle; that is environment-dependent and is not this
	 * clause.)  This is the same test src/unistd/write.c applies, and it
	 * is only correct because inherited descriptors now record their real
	 * access mode (src/internal/fd.c's accmode_of) -- before that, every
	 * descriptor inherited across a spawn read back as O_RDONLY and this
	 * check would have refused writable ones.
	 *
	 * Placed after the [ESPIPE] arm on purpose.  A pipe's read end
	 * satisfies both clauses at once and POSIX orders neither, but
	 * [ESPIPE] says the more useful thing about a pipe, and the existing
	 * assertion for it predates this check. */
	if ((f->flags & O_ACCMODE) == O_RDONLY) return EBADF;
	/* posix_fallocate.html ERRORS, *shall fail*: "[ENODEV] The fd argument
	 * does not refer to a regular file."  This used to be spelled
	 * `if (si.Directory) return EBADF;` -- a comment that named the right
	 * condition over code that returned the wrong errno for it, and that
	 * tested only one of the several ways a descriptor can fail to be a
	 * regular file.  [EBADF] on this page means two OTHER things ("is not
	 * a valid file descriptor", and "was opened without write
	 * permission"), so a caller that distinguishes them was told something
	 * false about its own descriptor.
	 *
	 * __FD_FILE is exactly the right predicate: __handle_type() assigns it
	 * to a disk/network/CD/tape file-system object that is NOT a directory
	 * (directories become __FD_DIR), so everything else -- a character
	 * device such as NUL, a console, a socket, a pipe -- is by
	 * construction not a regular file.  A pipe never reaches here because
	 * [ESPIPE] above is more specific and POSIX gives it its own clause. */
	if (f->type != __FD_FILE) return ENODEV;
	want = (long long)offset + (long long)len;
	if (want < 0) return EFBIG;

	st = NtQueryInformationFile(f->h, &io, &si, sizeof si, FileStandardInformation);
	if (!NT_SUCCESS(st)) return __errno_from_status(st);

	/* The second half of this guard is a data-loss interlock, not an
	 * optimisation.  ZwSetInformationFile(FileAllocationInformation) is
	 * documented (ntifs.h FILE_ALLOCATION_INFORMATION, "Remarks") as: "If
	 * the allocation size is set to a value that is less than the
	 * end-of-file position, the end-of-file position is automatically
	 * adjusted to match the allocation size."  The requested size is also
	 * rounded up to the filesystem's cluster size first, so the value that
	 * is compared against EndOfFile is not the one passed in.
	 *
	 * `want > si.AllocationSize` alone is safe only while AllocationSize
	 * >= EndOfFile, which is the ordinary NTFS case but is exactly what a
	 * sparse or compressed file breaks: such a file's allocation is
	 * deliberately smaller than its size.  On one -- EndOfFile 16384,
	 * AllocationSize 0 -- posix_fallocate(fd, 0, 100) would have passed
	 * the guard, requested an allocation of 100, had it rounded to one
	 * cluster, and had the file truncated to that cluster.  POSIX
	 * (posix_fallocate) never shrinks a file: "If the offset+len is beyond
	 * the current file size, then posix_fallocate() shall adjust the file
	 * size"; below that it changes no size at all.  So requesting an
	 * allocation smaller than the current EndOfFile is never something
	 * this function may do.
	 *
	 * Skipping the call is preferred to clamping the request up to
	 * si.EndOfFile.  Clamping would satisfy the allocation guarantee for
	 * the requested range, but it would also de-sparsify the entire file:
	 * posix_fallocate(fd, 0, 1) on a terabyte-sized sparse file would ask
	 * for a terabyte of clusters and most likely return ENOSPC.  Turning a
	 * hundred-byte request into a whole-file materialisation -- or into a
	 * hard failure -- is a worse outcome than under-delivering an
	 * allocation guarantee on a file shape whose whole purpose is to not
	 * have that allocation.  Nothing is destroyed either way.
	 *
	 * DO NOT DELETE THE SECOND CONJUNCT AS REDUNDANT.  It reads that way
	 * from here -- on a file whose AllocationSize >= EndOfFile, want >
	 * AllocationSize already implies want > EndOfFile -- and that is true
	 * of real NTFS, where a file extended with SetEndOfFile gets real
	 * clusters.  It is not true under Wine, which implements extension
	 * with ftruncate(), producing a hole: st_blocks is 0, so
	 * AllocationSize reads 0 for an ORDINARY file created the normal way,
	 * not merely for one deliberately marked sparse.  Under Wine the
	 * first test is therefore trivially true in the common case and this
	 * conjunct is the only thing preventing the truncation.  Measured on
	 * Windows 11 22621 by the Wine-divergence session: a non-sparse file
	 * of EndOfFile 16384 reports AllocationSize 16384 on NTFS, and 0
	 * under Wine.  (A genuinely sparse file reports 0 on both -- that
	 * part Wine gets right.)
	 *
	 * Not reproduced from inside this tree: ntlibc has no FSCTL_SET_SPARSE
	 * and Wine's FSCTL_SET_ZERO_DATA returns STATUS_NOT_SUPPORTED, so a
	 * deliberately sparse file cannot be built here.  That negative result
	 * is what the interlock was written without -- it rests on the
	 * documented FileAllocationInformation rule above.  The Wine finding
	 * arrived afterwards and says the guard is exercised in practice
	 * anyway, by ordinary files, without anyone creating a sparse one. */
	if (want > si.AllocationSize && want >= si.EndOfFile) {
		ai.AllocationSize = want;
		st = NtSetInformationFile(f->h, &io, &ai, sizeof ai, FileAllocationInformation);
		/* Real Windows honours this; Wine's ntdll does not implement
		 * FileAllocationInformation at all (it appears only in the
		 * set-info size table in dlls/ntdll/unix/file.c and falls
		 * through to the default arm) and every other failure short of
		 * that is a real error worth reporting (e.g. ENOSPC). Falling
		 * through on "no such information class here" still leaves the
		 * EndOfFile extension below to grow the file -- a strict
		 * reading of posix_fallocate() loses the "no later write can
		 * ENOSPC" guarantee on such a system, but the alternative is
		 * failing a real Windows-capable call every time it merely runs
		 * under Wine, which is worse than the degraded guarantee.
		 *
		 * Branch on the *status*, not on __errno_from_status().  The
		 * errno mapping is a lossy projection: it folds many distinct
		 * statuses onto one value, so a test against it silently
		 * widens.  Concretely, Wine reports the same missing set-info
		 * case as STATUS_NOT_IMPLEMENTED natively but as
		 * STATUS_INVALID_INFO_CLASS under WOW64; the latter maps to
		 * EINVAL, so an ENOSYS test tolerated the gap on x86_64 and
		 * rejected it on i386.  Widening the test to EINVAL would be
		 * worse still -- EINVAL also carries STATUS_INVALID_PARAMETER,
		 * STATUS_INFO_LENGTH_MISMATCH and STATUS_DATATYPE_MISALIGNMENT,
		 * turning this fallback into a bug-hider.  Whenever the status
		 * is in hand, decide from it. */
		if (!NT_SUCCESS(st)
		    && st != STATUS_NOT_IMPLEMENTED
		    && st != STATUS_NOT_SUPPORTED
		    && st != STATUS_INVALID_DEVICE_REQUEST
		    && st != STATUS_INVALID_INFO_CLASS)
			return __errno_from_status(st);
	}
	if (want > si.EndOfFile) {
		eof.EndOfFile = want;
		st = NtSetInformationFile(f->h, &io, &eof, sizeof eof, FileEndOfFileInformation);
		if (!NT_SUCCESS(st)) return __errno_from_status(st);
	}
	return 0;
}
