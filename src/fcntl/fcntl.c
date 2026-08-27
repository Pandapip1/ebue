/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include "libc.h"

int fcntl(int fd, int cmd, ...)
{
	struct __fd *f = __fd_get(fd);
	va_list ap;
	intptr_t arg;

	if (!f) return -1;
	va_start(ap, cmd);
	arg = va_arg(ap, intptr_t);
	va_end(ap);

	switch (cmd) {
	case F_DUPFD:
	case F_DUPFD_CLOEXEC: {
		int nfd;
		HANDLE h;
		NTSTATUS st;
		if (arg < 0 || arg >= FD_MAX) { errno = EINVAL; return -1; }
		nfd = __fd_alloc((int)arg);
		if (nfd < 0) return -1;
		st = NtDuplicateObject(NtCurrentProcess(), f->h, NtCurrentProcess(), &h, 0,
		                       cmd == F_DUPFD_CLOEXEC ? 0 : OBJ_INHERIT, DUPLICATE_SAME_ACCESS);
		if (!NT_SUCCESS(st)) return __set_errno_status(st);
		__fd_install_at(nfd, h, (f->flags & ~O_CLOEXEC) | (cmd == F_DUPFD_CLOEXEC ? O_CLOEXEC : 0), f->type);
		__fds[nfd].pad = f->pad;
		__fds[nfd].vfs = f->vfs;
		__fds[nfd].vfs_native = f->vfs_native;
		__fds[nfd].vseen = f->vseen;
		__fds[nfd].vnext = f->vnext;
		__fds[nfd].peer_len = f->peer_len;
		memcpy(__fds[nfd].peer, f->peer, sizeof f->peer);
		return nfd;
	}
	case F_GETFD:
		return f->flags & O_CLOEXEC ? FD_CLOEXEC : 0;
	case F_SETFD: {
		HANDLE h;
		NTSTATUS st;
		unsigned want = arg & FD_CLOEXEC ? O_CLOEXEC : 0;
		if ((f->flags & O_CLOEXEC) == want) return 0;
		/* Inheritability is a property of the handle; remake it. */
		st = NtDuplicateObject(NtCurrentProcess(), f->h, NtCurrentProcess(), &h, 0,
		                       want ? 0 : OBJ_INHERIT, DUPLICATE_SAME_ACCESS);
		if (!NT_SUCCESS(st)) return __set_errno_status(st);
		NtClose(f->h);
		f->h = h;
		__mq_fd_replaced(fd, h);
		f->flags = (f->flags & ~O_CLOEXEC) | want;
		return 0;
	}
	case F_GETFL:
		return f->flags & (O_ACCMODE | O_APPEND | O_NONBLOCK);
	case F_SETFL:
		f->flags = (f->flags & ~(O_APPEND | O_NONBLOCK)) | (arg & (O_APPEND | O_NONBLOCK));
		return 0;
	case F_GETLK: {
		struct flock *l = (struct flock *)arg;
		l->l_type = F_UNLCK;
		return 0;
	}
	case F_SETLK:
	case F_SETLKW:
		/* Advisory locks are not implemented; report success, the way
		 * a filesystem without locking support would. */
		return 0;
	default:
		errno = EINVAL;
		return -1;
	}
}
