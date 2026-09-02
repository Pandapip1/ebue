/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux's half of the src/internal/nt/handle_path.c split -- see that
 * file's own banner for why __handle_path() needed one at all: it is
 * called unconditionally from portable front doors (src/stat/chmod.c's
 * fchmod() EACCES retry, src/unistd/chdir.c's fchdir(),
 * src/process/exec.c, src/stdlib/realpath.c), not from anything already
 * gated behind nt/, so a Linux build that reaches any of them needs a
 * real body here, not just a working NT one.
 *
 * NT has no reverse mapping from a HANDLE back to a path at all -- that
 * is the whole reason src/internal/nt/handle_path.c has to ask the
 * object manager for the underlying device name and then search every
 * drive letter's symbolic link for a match. Linux does not need any of
 * that: the kernel already publishes exactly this mapping for every
 * open descriptor of the calling process, as the symlink target of
 * /proc/self/fd/<fd> (proc(5)), and readlinkat(2) reads a symlink's
 * target directly -- the same real primitive src/unistd/linux/
 * plat_unistd.c's __plat_readlink() already uses for readlink() itself.
 * No raw syscall trampoline is shared with that file (this tree's own
 * one-syscall-table-per-file discipline -- see this directory's other
 * files, e.g. plat_fd_init.c's banner), but the two are the identical
 * three-line SYS_readlinkat call.
 *
 * One honest gap, inherited from /proc/self/fd itself rather than
 * introduced here: if the descriptor's file was unlink()ed while still
 * open, the kernel appends " (deleted)" to the symlink target (proc(5),
 * "readlink(2) ... the deleted files are indicated by ' (deleted)'
 * appended to the pathname"), so the string this returns for such a
 * descriptor is not a path that can be reopened. That is a real
 * limitation of /proc/self/fd itself, not a shortcut taken here, and it
 * matches glibc's own realpath()/canonicalize_file_name() behaviour on
 * a deleted-but-open fd -- nothing in this tree's own OPTS-relevant
 * callers (fchmod's reopen, fchdir, realpath) reaches this path on a
 * file that was just created and is still linked.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include "libc.h"

/* Same 6-argument raw syscall trampoline every Linux backend defines
 * for itself. File-scoped by convention, not shared, the same as every
 * other Linux backend in this tree. */
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6) // NOLINT(bugprone-easily-swappable-parameters) -- raw syscall ABI slots are positional and semantically distinct
{
	register long x0 __asm__("x0") = a1;
	register long x1 __asm__("x1") = a2;
	register long x2 __asm__("x2") = a3;
	register long x3 __asm__("x3") = a4;
	register long x4 __asm__("x4") = a5;
	register long x5 __asm__("x5") = a6;
	register long x8 __asm__("x8") = nr;
	__asm__ volatile("svc #0"
	                 : "+r"(x0)
	                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
	                 : "memory", "cc");
	return x0;
}

#define SYS_readlinkat 78

static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

/* HANDLE here is always this backend's own boxed (fd + 1) encoding
 * (src/unistd/linux/plat_fd.c's banner) -- every caller of this function
 * already holds a __plat_handle_t this backend itself produced. */
static int unbox(HANDLE h)
{
	return (int)((long)h - 1);
}

/* "/proc/self/fd/" plus up to 10 decimal digits (a 32-bit fd never needs
 * more) plus the terminator. Written by hand rather than through
 * snprintf(): this file has no other reason to touch the stdio subsystem
 * at all, and a two's-complement int's decimal form is short enough that
 * pulling that whole chain in for it would be a needless dependency, not
 * a simplification. */
static void fd_path(int fd, char *out)
{
	static const char prefix[] = "/proc/self/fd/";
	char digits[10];
	int nd = 0;
	size_t i;

	for (i = 0; prefix[i]; i++) out[i] = prefix[i];
	if (fd == 0) {
		digits[nd++] = '0';
	} else {
		unsigned u = (unsigned)fd; /* fd is never negative here: unbox()'s
		                            * caller already rejected that case. */
		while (u) { digits[nd++] = (char)('0' + u % 10); u /= 10; }
	}
	while (nd) out[i++] = digits[--nd];
	out[i] = 0;
}

withtok(internal_heap_allocated)
char *__handle_path(HANDLE h)
{
	int fd = unbox(h);
	char path[32];
	char buf[PATH_MAX];
	char *r;
	long n;
	size_t bytes;

	if (fd < 0) { errno = EBADF; return 0; }
	fd_path(fd, path);
	n = raw_syscall(SYS_readlinkat, (long)AT_FDCWD, (long)path, (long)buf, (long)sizeof buf, 0L, 0L);
	if (is_sys_error(n)) { errno = (int)-n; return 0; }
	/* readlinkat(2) never NUL-terminates; it fills up to n bytes and
	 * reports the count, exactly like the POSIX readlink() this mirrors
	 * (see __plat_readlink()'s own comment on the same call). */
	if (!__size_add_checked((size_t)n, 1, &bytes)) { errno = ENOMEM; return 0; }
	r = __malloc(bytes);
	if (!r) return 0;
	if (n) {
		size_t i;
		for (i = 0; i < (size_t)n; i++) r[i] = buf[i];
	}
	r[n] = 0;
	return r;
}

// NOLINTEND(misc-include-cleaner)
