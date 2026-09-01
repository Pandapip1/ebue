/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The descriptor table: small integers over a __plat_handle_t.
 *
 * Genuinely portable bookkeeping only: the table itself (__fds[]),
 * allocation, and the plain accessors. Everything that once lived here
 * and is actually platform-specific -- __handle_type() (classify a
 * handle by querying the OS), __fd_init() (descriptors 0-2 plus
 * whatever a parent process handed down), __fd_runtime_data() (the
 * inheritance blob a child reads back) -- has moved to
 * src/internal/$(PLATFORM)/plat_fd_init.c (see src/internal/nt/
 * plat_fd_init.c's own banner for the split and why it was mechanical,
 * not a rewrite; Makefile's PLAT_GLOBS comment for the override
 * mechanism that makes both platforms' versions link cleanly without
 * colliding).
 *
 * Descriptors 0, 1 and 2 and everything __fd_init() does with them is
 * entirely a platform concern (NT: the PEB's process parameters and an
 * msvcrt-compatible RuntimeData inheritance blob; Linux: descriptors
 * the kernel already has open) -- see each platform's own plat_fd_init.c
 * for the real story, not this file.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

struct __fd __fds[FD_MAX];
int __fd_limit = FD_MAX;

int __fd_alloc(int lowest)
{
	int i, entries_left = FD_MAX;
	if (lowest < 0) lowest = 0;
	/* __fd_limit, not FD_MAX: setrlimit(RLIMIT_NOFILE) lowers it, and
	 * "a number one greater than the maximum value that the system may
	 * assign to a newly-created descriptor" (setrlimit.html) is exactly
	 * this bound.  It never exceeds FD_MAX, so the table stays in
	 * range whatever a caller asks for. */
	for (i = lowest; i < __fd_limit && entries_left > 0;
	     i++, entries_left--)
		if (!__fds[i].h) return i;
	errno = EMFILE;
	return -1;
}

int __fd_install_at(int fd, HANDLE h, unsigned flags, int type) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct __fd *f = &__fds[fd];
	memset(f, 0, sizeof *f);
	f->h = h;
	f->flags = flags;
	f->type = (unsigned char)(type ? type : __handle_type(h));
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

HANDLE __fd_handle(int fd)
{
	struct __fd *f = __fd_get(fd);
	return f ? f->h : 0;
}

int __fd_close_all_cloexec(void)
{
	int i;
	for (i = 0; i < FD_MAX; i++)
		if (__fds[i].h && (__fds[i].flags & O_CLOEXEC)) close(i);
	return 0;
}

// NOLINTEND(misc-include-cleaner)
