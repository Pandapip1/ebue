/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT implementation of src/internal/plat_fd.h -- see that header for
 * the contract each function makes.  Everything here was, until this
 * file existed, inline inside src/unistd/{close,read,write,lseek,
 * dup}.c; nothing changed in substance, only location and the addition
 * of a POSIX-shaped return (errno already set) in place of a raw
 * NTSTATUS or a signal the caller had to raise itself.
 */
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include "libc.h"
#include "plat_fd.h"

int __plat_close(__plat_handle_t h)
{
	NTSTATUS st = NtClose(h);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

ssize_t __plat_read(__plat_handle_t h, void *buf, size_t count)
{
	IO_STATUS_BLOCK io;
	NTSTATUS st;

	io.Status = 0; io.Information = 0;
	st = NtReadFile(h, 0, 0, 0, &io, buf, (ULONG)count, 0, 0);
	if (st == STATUS_PENDING) {
		/* The handle was not opened synchronous (inherited from a
		 * parent that opened it overlapped, say): wait for it. */
		NtWaitForSingleObject(h, 0, 0);
		st = io.Status;
	}
	if (st == STATUS_END_OF_FILE || st == STATUS_PIPE_BROKEN || st == STATUS_PIPE_DISCONNECTED) return 0;
	if (st == STATUS_PIPE_EMPTY) { errno = EAGAIN; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return (ssize_t)io.Information;
}

ssize_t __plat_pread(__plat_handle_t h, void *buf, size_t count, off_t off)
{
	IO_STATUS_BLOCK io;
	LARGE_INTEGER pos = off;
	NTSTATUS st;

	io.Information = 0;
	st = NtReadFile(h, 0, 0, 0, &io, buf, (ULONG)count, &pos, 0);
	if (st == STATUS_PENDING) { NtWaitForSingleObject(h, 0, 0); st = io.Status; }
	if (st == STATUS_END_OF_FILE) return 0;
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return (ssize_t)io.Information;
}

/* write.html, ERRORS, shall fail: "[EFBIG] The file is a regular file,
 * nbyte is greater than 0, and the starting position is greater than or
 * equal to the offset maximum established in the open file description
 * associated with fildes."  The starting position of a write() is the
 * file position, or the end of the file when `append` is set (that is
 * what FILE_WRITE_TO_END_OF_FILE below resolves to).
 *
 * WHY THIS IS ASKED ON THE FAILURE PATH rather than before the write.
 * The alternative is an unconditional NtQueryInformationFile in front of
 * every NtWriteFile -- one extra round trip on the hottest path in the
 * library, paid by every stdio flush, to evaluate a condition that no
 * successful write can ever satisfy.  A starting position at or past
 * __OFF_MAX cannot produce a successful transfer: there is nowhere for
 * the bytes to go.  So the query is only worth making once NT has
 * already refused, where it costs nothing and where its only effect is
 * to replace one error report with the one POSIX names.  A shall-fail
 * clause that turns an error into a *different* error is exactly the
 * shape that suits this placement.
 *
 * A query that cannot be answered reports 0 -- the caller then gets
 * NT's own status, translated normally, which is what it would have
 * got before. */
static int start_at_offset_max(__plat_handle_t h, int append)
{
	IO_STATUS_BLOCK io;

	if (append) {
		FILE_STANDARD_INFORMATION si;
		if (!NT_SUCCESS(NtQueryInformationFile(h, &io, &si, sizeof si, FileStandardInformation)))
			return 0;
		return si.EndOfFile >= __OFF_MAX;
	} else {
		FILE_POSITION_INFORMATION pi;
		if (!NT_SUCCESS(NtQueryInformationFile(h, &io, &pi, sizeof pi, FilePositionInformation)))
			return 0;
		return pi.CurrentByteOffset >= __OFF_MAX;
	}
}

ssize_t __plat_write(__plat_handle_t h, const void *buf, size_t count, int append)
{
	IO_STATUS_BLOCK io;
	LARGE_INTEGER pos, *pp = 0;
	NTSTATUS st;

	if (append) {
		pos = FILE_WRITE_TO_END_OF_FILE;
		pp = &pos;
	}
	io.Status = 0; io.Information = 0;
	st = NtWriteFile(h, 0, 0, 0, &io, buf, (ULONG)count, pp, 0);
	if (st == STATUS_PENDING) { NtWaitForSingleObject(h, 0, 0); st = io.Status; }
	/* Raised here, not by the front door testing errno==EPIPE after the
	 * fact: __errno_from_status()'s generic table also maps OTHER
	 * statuses (STATUS_PIPE_NOT_AVAILABLE, some DOS codes) to EPIPE, and
	 * SIGPIPE must not fire for those -- only a genuinely broken/
	 * disconnected/closing pipe raises it.  Only this call, which still
	 * has the real status in hand, can tell the two apart. */
	if (st == STATUS_PIPE_BROKEN || st == STATUS_PIPE_DISCONNECTED || st == STATUS_PIPE_CLOSING) {
		__sig_lock();
		__raise_internal(SIGPIPE);
		__sig_unlock();
		errno = EPIPE;
		return -1;
	}
	if (!NT_SUCCESS(st)) {
		/* The offset maximum, NOT the process limit: this [EFBIG] is a
		 * property of the open file description, so setrlimit.html's
		 * "SIGXFSZ shall be generated" does not reach it and nothing is
		 * raised here.  (__fsize_exceeded(), the process-limit half,
		 * already ran in the front door before this call.) */
		if (start_at_offset_max(h, append)) { errno = EFBIG; return -1; }
		return __set_errno_status(st);
	}
	return (ssize_t)io.Information;
}

ssize_t __plat_pwrite(__plat_handle_t h, const void *buf, size_t count, off_t off)
{
	IO_STATUS_BLOCK io;
	LARGE_INTEGER pos = off;
	NTSTATUS st;

	io.Information = 0;
	st = NtWriteFile(h, 0, 0, 0, &io, buf, (ULONG)count, &pos, 0);
	if (st == STATUS_PENDING) { NtWaitForSingleObject(h, 0, 0); st = io.Status; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return (ssize_t)io.Information;
}

long long __plat_seek_query(__plat_handle_t h, int at_eof)
{
	IO_STATUS_BLOCK io;
	NTSTATUS st;

	if (at_eof) {
		FILE_STANDARD_INFORMATION si;
		st = NtQueryInformationFile(h, &io, &si, sizeof si, FileStandardInformation);
		if (!NT_SUCCESS(st)) return __set_errno_status(st);
		return si.EndOfFile;
	} else {
		FILE_POSITION_INFORMATION pi;
		st = NtQueryInformationFile(h, &io, &pi, sizeof pi, FilePositionInformation);
		if (!NT_SUCCESS(st)) return __set_errno_status(st);
		return pi.CurrentByteOffset;
	}
}

int __plat_seek_set(__plat_handle_t h, long long target)
{
	IO_STATUS_BLOCK io;
	FILE_POSITION_INFORMATION pi;
	NTSTATUS st;

	pi.CurrentByteOffset = target;
	st = NtSetInformationFile(h, &io, &pi, sizeof pi, FilePositionInformation);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int __plat_dup(__plat_handle_t h, int inheritable, __plat_handle_t *out)
{
	NTSTATUS st = NtDuplicateObject(NtCurrentProcess(), h, NtCurrentProcess(), out, 0,
	                                inheritable ? OBJ_INHERIT : 0, DUPLICATE_SAME_ACCESS);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}
