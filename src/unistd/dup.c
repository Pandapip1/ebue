/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "libc.h"

static int dup_to(int fd, int newfd, int cloexec)
{
	struct __fd *f = __fd_get(fd);
	struct __fd state;
	HANDLE h;
	NTSTATUS st;
	if (!f) return -1;
	state = *f;
	st = NtDuplicateObject(NtCurrentProcess(), f->h, NtCurrentProcess(), &h, 0,
	                       cloexec ? 0 : OBJ_INHERIT, DUPLICATE_SAME_ACCESS);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	if (__fds[newfd].h) NtClose(__fds[newfd].h);
	__fd_install_at(newfd, h, (state.flags & ~O_CLOEXEC) | (cloexec ? O_CLOEXEC : 0), state.type);
	__fds[newfd].pad = state.pad;
	__fds[newfd].vfs = state.vfs;
	__fds[newfd].vfs_native = state.vfs_native;
	__fds[newfd].vseen = state.vseen;
	__fds[newfd].vnext = state.vnext;
	__fds[newfd].peer_len = state.peer_len;
	memcpy(__fds[newfd].peer, state.peer, sizeof state.peer);
	return newfd;
}

int dup(int fd)
{
	int nfd;
	if (!__fd_get(fd)) return -1;
	nfd = __fd_alloc(0);
	if (nfd < 0) return -1;
	return dup_to(fd, nfd, 0);
}

int dup3(int fd, int newfd, int flags)
{
	if (newfd < 0 || newfd >= FD_MAX || fd == newfd) { errno = fd == newfd ? EINVAL : EBADF; return -1; }
	return dup_to(fd, newfd, flags & O_CLOEXEC);
}

int dup2(int fd, int newfd)
{
	if (fd == newfd) return __fd_get(fd) ? fd : -1;
	if (newfd < 0 || newfd >= FD_MAX) { errno = EBADF; return -1; }
	return dup_to(fd, newfd, 0);
}
