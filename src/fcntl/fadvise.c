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
	want = (long long)offset + (long long)len;
	if (want < 0) return EFBIG;

	st = NtQueryInformationFile(f->h, &io, &si, sizeof si, FileStandardInformation);
	if (!NT_SUCCESS(st)) return __errno_from_status(st);
	if (si.Directory) return EBADF;   /* not a regular file */

	if (want > si.AllocationSize) {
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
