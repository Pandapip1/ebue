/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>
#include "libc.h"

ssize_t read(int fd, void *buf, size_t count)
{
	struct __fd *f = __fd_get(fd);
	IO_STATUS_BLOCK io;
	NTSTATUS st;

	if (!f) return -1;
	/* A socket goes through IOCTL_AFD_RECV (src/socket/sendrecv.c), not
	 * NtReadFile -- test/networking-audit.md sec 2 flags plain
	 * NtReadFile/NtWriteFile against a socket handle as unverified, and
	 * recv()/send() already have this fd's dispatch and error mapping,
	 * so read()/write() just forward to them rather than duplicating
	 * either. */
	if (f->type == __FD_SOCKET) return recv(fd, buf, count, 0);
	if ((f->flags & O_ACCMODE) == O_WRONLY) { errno = EBADF; return -1; }
	if (f->type == __FD_DIR) { errno = EISDIR; return -1; }
	if (count > 0x7fffffff) count = 0x7fffffff;
	if (!count) return 0;

	io.Status = 0; io.Information = 0;
	st = NtReadFile(f->h, 0, 0, 0, &io, buf, (ULONG)count, 0, 0);
	if (st == STATUS_PENDING) {
		/* The handle was not opened synchronous (inherited from a
		 * parent that opened it overlapped, say): wait for it. */
		NtWaitForSingleObject(f->h, 0, 0);
		st = io.Status;
	}
	if (st == STATUS_END_OF_FILE || st == STATUS_PIPE_BROKEN || st == STATUS_PIPE_DISCONNECTED) return 0;
	if (st == STATUS_PIPE_EMPTY) { errno = EAGAIN; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return (ssize_t)io.Information;
}

ssize_t pread(int fd, void *buf, size_t count, off_t off)
{
	struct __fd *f = __fd_get(fd);
	IO_STATUS_BLOCK io;
	LARGE_INTEGER pos = off;
	long long saved;
	NTSTATUS st;

	if (!f) return -1;
	if ((f->flags & O_ACCMODE) == O_WRONLY) { errno = EBADF; return -1; }
	if (f->type != __FD_FILE) { errno = ESPIPE; return -1; }
	if (count > 0x7fffffff) count = 0x7fffffff;
	/* NT moves a synchronous handle's position to the end of a positioned
	 * transfer; POSIX says it must not move.  See src/internal/fdpos.c. */
	if (__fd_pos_save(f->h, &saved) < 0) return -1;
	io.Information = 0;
	st = NtReadFile(f->h, 0, 0, 0, &io, buf, (ULONG)count, &pos, 0);
	if (st == STATUS_PENDING) { NtWaitForSingleObject(f->h, 0, 0); st = io.Status; }
	__fd_pos_restore(f->h, saved);
	if (st == STATUS_END_OF_FILE) return 0;
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return (ssize_t)io.Information;
}
