/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include "libc.h"

ssize_t write(int fd, const void *buf, size_t count)
{
	struct __fd *f = __fd_get(fd);
	IO_STATUS_BLOCK io;
	LARGE_INTEGER pos, *pp = 0;
	NTSTATUS st;

	if (!f) return -1;
	if ((f->flags & O_ACCMODE) == O_RDONLY) { errno = EBADF; return -1; }
	if (f->type == __FD_DIR) { errno = EISDIR; return -1; }
	if (count > 0x7fffffff) count = 0x7fffffff;
	if (!count) return 0;

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
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
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
	if (count > 0x7fffffff) count = 0x7fffffff;
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
