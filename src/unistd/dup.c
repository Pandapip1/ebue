/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "plat_fd.h"

static int dup_to(int fd, int newfd, int cloexec) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct __fd *f = __fd_get(fd);
	struct __fd state;
	__plat_handle_t h;
	if (!f) return -1;
	state = *f;
	/* __plat_dup_to(), not __plat_dup(): newfd is not merely the table
	 * slot this duplicate is filed under, it is the descriptor NUMBER
	 * dup2()/dup3() are specified to produce -- see plat_fd.h's own
	 * comment on why only the "_to" variant guarantees that on every
	 * backend. Passing __fds[newfd].h through as `old` (rather than
	 * closing it here first, or after) hands its disposal to the one
	 * place that knows how to do it safely for this backend -- see
	 * that same comment for why closing it ourselves, on either side
	 * of the call, is not simply equivalent. */
	if (__plat_dup_to(f->h, newfd, __fds[newfd].h, !cloexec, &h) < 0) return -1;
	__fd_install_at(newfd, h, (state.flags & ~O_CLOEXEC) | (cloexec ? O_CLOEXEC : 0), state.type);
	__fds[newfd].pad = state.pad;
	__fds[newfd].shm_mode_valid = state.shm_mode_valid;
	__fds[newfd].shm_mode = state.shm_mode;
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

// NOLINTEND(misc-include-cleaner)
