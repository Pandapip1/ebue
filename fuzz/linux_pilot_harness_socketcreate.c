/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test-harness scaffolding for the Linux socket-CREATION pilot -- NOT
 * part of ntlibc, same standing as fuzz/linux_pilot_harness_socket.c
 * (the earlier recv()/send()-only pilot's own harness).
 *
 * Unlike that earlier harness, this one DOES need a real __fd_install():
 * src/socket/socket.c's and src/socket/accept.c's front doors both
 * install a freshly opened descriptor themselves now (socket() calls
 * __plat_socket_open() then __fd_install(); accept() calls
 * __plat_socket_accept() then __fd_install()), rather than the earlier
 * pilot's test code doing the installation by hand against a
 * pre-connected socketpair(2).
 *
 * __fd_install()/__fd_install_at() are reimplemented here, minimally,
 * rather than linking the real src/internal/fd.c, for the exact same
 * reason fuzz/linux_pilot_harness.c and fuzz/linux_pilot_harness_socket.c
 * both give for their own from-scratch fd tables: the real
 * __fd_install_at() falls back to __handle_type(h) whenever its `type`
 * argument is 0 (`f->type = (unsigned char)(type ? type : __handle_type(h));`),
 * and __handle_type() unconditionally references NT-only ntdll entry
 * points that have no definition in a native Linux link -- a link-time
 * failure even though every call in this pilot always passes a nonzero
 * type (__FD_SOCKET) and so never actually reaches that branch at
 * runtime.  This file's own __fd_install_at() skips the fallback
 * entirely and just stores `type` directly, so __handle_type is never
 * referenced at all.
 */
#include <string.h>
#include "libc.h"

struct __fd __fds[FD_MAX];
int __fd_limit = FD_MAX;

int __fd_alloc(int lowest)
{
	int i;
	if (lowest < 0) lowest = 0;
	for (i = lowest; i < __fd_limit; i++)
		if (!__fds[i].h) return i;
	errno = EMFILE;
	return -1;
}

int __fd_install_at(int fd, HANDLE h, unsigned flags, int type)
{
	struct __fd *f = &__fds[fd];
	memset(f, 0, sizeof *f);
	f->h = h;
	f->flags = flags;
	f->type = (unsigned char)type;
	f->pos = -1;
	return fd;
}

int __fd_install(HANDLE h, unsigned flags, int type)
{
	int fd = __fd_alloc(0);
	if (fd < 0) return -1;
	return __fd_install_at(fd, h, flags, type);
}

struct __fd *__fd_get(int fd)
{
	if (fd < 0 || fd >= FD_MAX || !__fds[fd].h) { errno = EBADF; return 0; }
	return &__fds[fd];
}
