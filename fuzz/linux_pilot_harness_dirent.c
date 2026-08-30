/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test-harness scaffolding for the Linux dirent pilot -- NOT part of
 * ntlibc, exactly like fuzz/linux_pilot_harness_fs.c (the filesystem-
 * subsystem pilot's own harness) and fuzz/linux_pilot_harness.c are "not
 * part of ntlibc" for their respective native builds. A separate file
 * rather than a reuse of linux_pilot_harness_fs.c: this pilot's link set
 * is much smaller (no fcntl()/flock()/ioctl()/stat() front doors at
 * all -- see tools/linux-build-dirent.sh), and needs one symbol none of
 * the existing harnesses provide: __fd_handle() (src/dirent/readdir.c's
 * __dirstream_next() is the first Linux-pilot-linked front door to call
 * it -- every earlier pilot's front-door set happened not to need it).
 *
 * The fd table (__fds[]/__fd_limit/__fd_alloc/__fd_install/
 * __fd_install_at/__fd_get/__fd_handle) is reimplemented here for the
 * identical reason every other pilot harness's own banner gives: linking
 * the real src/internal/fd.c would still require satisfying NT-only
 * syscalls this pilot has no Linux backend for at all.
 *
 * __mq_fd_closed(): src/unistd/close.c calls it unconditionally at the
 * top of close(); this pilot installs no mqueue descriptors, so it is a
 * no-op, the same shape as every other pilot harness's own copy.
 *
 * __malloc()/__free(): src/dirent/opendir.c allocates the DIR and its
 * __DIRBUF_SIZE record buffer through these, and src/dirent/closedir.c
 * frees them -- but the real implementation (src/malloc/malloc.c) is
 * RtlAllocateHeap on NT's own process heap (see that file's own banner),
 * as NT-specific as fd.c itself and equally out of scope for a Linux
 * pilot. A trivial bump allocator stands in: this test opens one
 * directory, reads its entries, and closes it -- at most a handful of
 * __malloc() calls for the lifetime of the whole process -- so a static
 * arena with a no-op __free() (a deliberate, bounded leak, not a
 * correctness gap this test cares about) is sufficient, same spirit as
 * every other "the linker needs a real symbol, but this pilot does not
 * actually exercise its logic" stand-in the existing harnesses already
 * use.
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
	f->type = (unsigned char)type; /* always nonzero in this pilot --
	                                * __plat_open() (src/fcntl/linux/
	                                * plat_fcntl.c) always names a real
	                                * __FD_* via statx(), never leaves
	                                * this 0 the way NT's auto-classifying
	                                * __handle_type() path might. */
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

void __mq_fd_closed(int fd)
{
	(void)fd;
}

/* A bump allocator over a static arena -- see this file's own banner for
 * why a real allocator is out of scope here. 1 MiB is far more than this
 * test's few DIR/__DIRBUF_SIZE (32768-byte) allocations need. */
static unsigned char arena[1 << 20];
static size_t arena_used;

void *__malloc(size_t n)
{
	void *p;
	/* 16-byte alignment: matches malloc.c's own real x86_64 alignment
	 * banner ("Alignment is 8 on i386 and 16 on x86_64"), and is enough
	 * for any type this pilot allocates (struct __dirstream has no
	 * member wider than a pointer/long long). */
	size_t aligned = (arena_used + 15u) & ~(size_t)15u;
	if (n == 0) n = 1;
	if (aligned + n > sizeof arena) { errno = ENOMEM; return 0; }
	p = arena + aligned;
	arena_used = aligned + n;
	return p;
}

void __free(void *p)
{
	(void)p; /* deliberate leak -- see this file's own banner */
}
