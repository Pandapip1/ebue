/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_fd.h -- see src/mman/linux/
 * plat_mem.c's own banner for the general discipline this file follows
 * too (raw syscall(2), no host libc, -nostdinc against ntlibc's own
 * headers, aarch64 syscall numbers confirmed against this host).
 *
 * __plat_handle_t encoding: NT's own HANDLE is already void*, so the NT
 * backend's __plat_handle_t is a direct pass-through -- but a Linux fd
 * is a small int, and fd 0 (stdin) is perfectly valid, while every
 * front door treats a NULL/zero __plat_handle_t as "no handle, empty
 * slot" (see struct __fd's own field comment in libc.h). Boxing fd 0
 * as literal 0 would make a valid stdin descriptor indistinguishable
 * from an empty slot. This file boxes a
 * real fd as (fd + 1) and unboxes by subtracting 1, so __PLAT_HANDLE_NULL
 * (0) never collides with any real fd -- entirely this backend's own
 * concern; the front door never sees or needs to know the encoding.
 *
 * Every NT-specific interpretation step plat_fd.h's own comment
 * describes -- STATUS_PENDING waits, broken-pipe-as-EOF, EFBIG's
 * offset-maximum query, SIGPIPE's status disambiguation -- collapses
 * to nothing here: a Linux read()/write() syscall already returns 0 at
 * EOF, already delivers SIGPIPE via the kernel's own default
 * disposition on a broken pipe (ntlibc's signal delivery still sees it
 * the normal POSIX way), and needs no offset-maximum probe at all.
 */
#include <errno.h>
#include "plat_fd.h"

/* aarch64 Linux syscall numbers -- see plat_mem.c's banner for why
 * these are hardcoded rather than pulled from a host header. */
#define SYS_close  57
#define SYS_lseek  62
#define SYS_read   63
#define SYS_write  64
#define SYS_pread64  67
#define SYS_pwrite64 68
#define SYS_dup    23
#define SYS_fcntl  25

#define F_SETFD_LX   2
#define FD_CLOEXEC_LX 1

extern long syscall(long number, ...);

static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

static int unbox(__plat_handle_t h)
{
	return (int)((long)h - 1);
}

int __plat_close(__plat_handle_t h)
{
	long ret = syscall(SYS_close, unbox(h));
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

ssize_t __plat_read(__plat_handle_t h, void *buf, size_t count)
{
	long ret = syscall(SYS_read, unbox(h), buf, count);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (ssize_t)ret;
}

ssize_t __plat_pread(__plat_handle_t h, void *buf, size_t count, off_t off)
{
	long ret = syscall(SYS_pread64, unbox(h), buf, count, (long)off);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (ssize_t)ret;
}

ssize_t __plat_write(__plat_handle_t h, const void *buf, size_t count, int append)
{
	long ret;
	/* `append` needs nothing here: on Linux, whether a write() goes to
	 * the file's current end is decided by the underlying fd's own
	 * O_APPEND bit (set when it was opened), not by any per-call
	 * argument the way NT's FILE_WRITE_TO_END_OF_FILE token needs to be
	 * -- the kernel already does the right thing for a plain write()
	 * regardless of which case this is. */
	(void)append;
	ret = syscall(SYS_write, unbox(h), buf, count);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (ssize_t)ret;
}

ssize_t __plat_pwrite(__plat_handle_t h, const void *buf, size_t count, off_t off)
{
	long ret = syscall(SYS_pwrite64, unbox(h), buf, count, (long)off);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (ssize_t)ret;
}

long long __plat_seek_query(__plat_handle_t h, int at_eof)
{
	int fd = unbox(h);
	long cur, ret;

	if (!at_eof) {
		/* SEEK_CUR with a zero offset is a pure query: it reports the
		 * position without moving it, exactly this contract's promise. */
		ret = syscall(SYS_lseek, fd, 0L, 1 /* SEEK_CUR */);
		if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
		return ret;
	}
	/* SEEK_END, unlike SEEK_CUR, WOULD move the descriptor's position
	 * as a side effect -- something this "just a query" contract must
	 * not do. Save and restore around it, the same pattern
	 * src/internal/fdpos.c already uses for a different NT-only quirk.
	 * A pure fstat(2) query would avoid the extra two syscalls, at the
	 * cost of this file needing to hardcode aarch64's raw kernel
	 * `struct stat` layout; not worth it for a pilot backend, and
	 * documented here rather than silently assumed correct. */
	cur = syscall(SYS_lseek, fd, 0L, 1 /* SEEK_CUR */);
	if (is_sys_error(cur)) { errno = (int)-cur; return -1; }
	ret = syscall(SYS_lseek, fd, 0L, 2 /* SEEK_END */);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	syscall(SYS_lseek, fd, cur, 0 /* SEEK_SET */);
	return ret;
}

int __plat_seek_set(__plat_handle_t h, long long target)
{
	long ret = syscall(SYS_lseek, unbox(h), (long)target, 0 /* SEEK_SET */);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_dup(__plat_handle_t h, int inheritable, __plat_handle_t *out)
{
	long newfd = syscall(SYS_dup, unbox(h));
	if (is_sys_error(newfd)) { errno = (int)-newfd; return -1; }
	if (!inheritable) {
		/* dup(2) never sets O_CLOEXEC on the new descriptor (matching
		 * `inheritable` true); ask for it explicitly otherwise. */
		long fc = syscall(SYS_fcntl, newfd, F_SETFD_LX, FD_CLOEXEC_LX);
		if (is_sys_error(fc)) {
			int e = (int)-fc;
			syscall(SYS_close, newfd);
			errno = e;
			return -1;
		}
	}
	*out = (__plat_handle_t)(newfd + 1);
	return 0;
}
