/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <errno.h>
#include "libc.h"

/* write.html, ERRORS, shall fail: "[EFBIG] The file is a regular file,
 * nbyte is greater than 0, and the starting position is greater than or
 * equal to the offset maximum established in the open file description
 * associated with fildes."  The starting position of a write() is the
 * file position, or the end of the file when O_APPEND is set (that is
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
 * NT's own status, which is what it would have got before. */
static int start_at_offset_max(struct __fd *f)
{
	IO_STATUS_BLOCK io;

	if (f->flags & O_APPEND) {
		FILE_STANDARD_INFORMATION si;
		if (!NT_SUCCESS(NtQueryInformationFile(f->h, &io, &si, sizeof si, FileStandardInformation)))
			return 0;
		return si.EndOfFile >= __OFF_MAX;
	} else {
		FILE_POSITION_INFORMATION pi;
		if (!NT_SUCCESS(NtQueryInformationFile(f->h, &io, &pi, sizeof pi, FilePositionInformation)))
			return 0;
		return pi.CurrentByteOffset >= __OFF_MAX;
	}
}

ssize_t write(int fd, const void *buf, size_t count)
{
	struct __fd *f = __fd_get(fd);
	IO_STATUS_BLOCK io;
	LARGE_INTEGER pos, *pp = 0;
	NTSTATUS st;

	if (!f) return -1;
	/* See read.c's matching branch: a socket is IOCTL_AFD_SEND, not
	 * NtWriteFile. */
	if (f->type == __FD_SOCKET) return send(fd, buf, count, 0);
	if ((f->flags & O_ACCMODE) == O_RDONLY) { errno = EBADF; return -1; }
	if (f->type == __FD_DIR) { errno = EISDIR; return -1; }
	if (count > 0x7fffffff) count = 0x7fffffff;
	if (!count) return 0;

	/* RLIMIT_FSIZE: clamp to the process file-size limit, or raise
	 * SIGXFSZ and fail with [EFBIG] when not one byte may be written
	 * (src/misc/resource.c's __fsize_exceeded(), which does both in the
	 * order setrlimit.html needs and may not return at all).  Only
	 * regular files have a size this can be about, and the predicate
	 * short-circuits when no limit is set, so an unlimited process pays
	 * nothing. */
	if (f->type == __FD_FILE && __fsize_limited()) {
		long long room = __fsize_clamp(f->h, (f->flags & O_APPEND) != 0, count);
		if (room < 0) return -1;
		count = (size_t)room;
	}

	if ((f->flags & O_APPEND) && f->type == __FD_FILE) {
		pos = FILE_WRITE_TO_END_OF_FILE;
		pp = &pos;
	}
	io.Status = 0; io.Information = 0;
	st = NtWriteFile(f->h, 0, 0, 0, &io, buf, (ULONG)count, pp, 0);
	if (st == STATUS_PENDING) { NtWaitForSingleObject(f->h, 0, 0); st = io.Status; }
	if (st == STATUS_PIPE_BROKEN || st == STATUS_PIPE_DISCONNECTED || st == STATUS_PIPE_CLOSING) {
		__raise_internal(SIGPIPE);
		errno = EPIPE;
		return -1;
	}
	if (!NT_SUCCESS(st)) {
		/* The offset maximum, NOT the process limit: this [EFBIG] is a
		 * property of the open file description, so setrlimit.html's
		 * "SIGXFSZ shall be generated" does not reach it and nothing is
		 * raised here.  Only __fsize_exceeded() above signals. */
		if (f->type == __FD_FILE && start_at_offset_max(f)) { errno = EFBIG; return -1; }
		return __set_errno_status(st);
	}
	return (ssize_t)io.Information;
}

ssize_t pwrite(int fd, const void *buf, size_t count, off_t off)
{
	struct __fd *f = __fd_get(fd);
	IO_STATUS_BLOCK io;
	LARGE_INTEGER pos = off;
	long long saved;
	NTSTATUS st;

	if (!f) return -1;
	if ((f->flags & O_ACCMODE) == O_RDONLY) { errno = EBADF; return -1; }
	if (f->type != __FD_FILE) { errno = ESPIPE; return -1; }
	if (off < 0) { errno = EINVAL; return -1; }
	if (count > 0x7fffffff) count = 0x7fffffff;
	/* The offset maximum, measured against the CALLER'S offset: for
	 * pwrite the "starting position" write.html's [EFBIG] speaks of is
	 * the offset argument, so unlike write() above this needs no query
	 * and is decided before anything is attempted.  The clamp below is
	 * the DESCRIPTION's other half -- "For regular files, no data
	 * transfer shall occur past the offset maximum established in the
	 * open file description associated with fildes" -- which turns a
	 * request straddling the maximum into a short write rather than an
	 * error.  Both arms are unsigned so that off + count cannot overflow
	 * a signed off_t on the way to being compared.  This is the file's
	 * limit and not the process's, so like write()'s matching arm it
	 * raises no SIGXFSZ. */
	if (count && off >= __OFF_MAX) { errno = EFBIG; return -1; }
	if ((unsigned long long)off + count > (unsigned long long)__OFF_MAX)
		count = (size_t)(__OFF_MAX - off);
	/* RLIMIT_FSIZE, measured against the CALLER'S offset rather than the
	 * file position -- pwrite writes where it is told.  Unlike the two
	 * offset-maximum arms above this one IS the process limit, so the
	 * refusal goes through __fsize_exceeded() and generates SIGXFSZ. */
	if (__fsize_limited()) {
		long long room = __fsize_room_at(off);
		if (room <= 0) return __fsize_exceeded();
		if ((long long)count > room) count = (size_t)room;
	}
	/* NT moves a synchronous handle's position to the end of a positioned
	 * transfer; POSIX says it must not move.  See src/internal/fdpos.c. */
	if (__fd_pos_save(f->h, &saved) < 0) return -1;
	io.Information = 0;
	st = NtWriteFile(f->h, 0, 0, 0, &io, buf, (ULONG)count, &pos, 0);
	if (st == STATUS_PENDING) { NtWaitForSingleObject(f->h, 0, 0); st = io.Status; }
	__fd_pos_restore(f->h, saved);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return (ssize_t)io.Information;
}
