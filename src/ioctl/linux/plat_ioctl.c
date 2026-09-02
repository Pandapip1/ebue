/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_ioctl.h -- see that header
 * for the contract each function makes, and src/mman/linux/plat_mem.c's
 * banner for the general discipline this file follows (raw syscall(2),
 * no host libc, -nostdinc against ntlibc's own headers, aarch64
 * syscall numbers confirmed against this host's own <sys/syscall.h> as
 * an oracle).
 *
 * Both functions here operate purely on an already-open __plat_handle_t
 * -- src/ioctl/ioctl.c never resolves a path, only ever reaches into
 * the fd table -- so, like src/file/linux/plat_flock.c, this is fully
 * portable: no NT-only path-resolution gap here.
 *
 * __plat_fionread_pipe() is a single native ioctl(FIONREAD) syscall,
 * the exact request this function's name already promises, versus NT's
 * FilePipeLocalInformation query standing in for it.  FIONREAD's value
 * (0x541b) is ntlibc's own <sys/ioctl.h> value, unchanged: it already
 * matches the Linux kernel ABI (this IS the Linux ioctl request
 * number), so no translation is needed -- the same situation plat_mem.c
 * describes for PROT_/MAP_.
 *
 * __plat_file_eof_and_pos() has no single Linux syscall that reports
 * "current EOF and current file position" together the way NT's
 * FileStandardInformation/FilePositionInformation pair does -- it is a
 * statx(2) call for the size plus an lseek(2) SEEK_CUR query for the
 * position (the identical SEEK_CUR-as-pure-query technique src/unistd/
 * linux/plat_fd.c's __plat_seek_query() already uses, duplicated here
 * rather than shared across translation units, matching every other
 * Linux backend file's own-syscall-table discipline).
 *
 * statx(2), not a raw fstat(2)/newfstatat(2), is used for the size
 * query: struct statx is a fixed, architecture-INDEPENDENT ABI (unlike
 * the classic kernel `struct stat`, whose raw layout genuinely differs
 * across architectures -- x86_64's is not aarch64's), so the local
 * mirror of it below needs no per-architecture variant the way this
 * whole file's syscall numbers already do. Confirmed field-for-field
 * against this host's own <linux/stat.h> (offsetof/sizeof dump), not
 * assumed -- see src/stat/linux/plat_stat.c's own banner, which uses
 * the identical struct and makes the fuller case for it (this file only
 * needs stx_size out of it).
 *
 * __plat_tiocgwinsz() is the third function this file implements, added
 * alongside the original two rather than in a file of its own: a
 * single ioctl(TIOCGWINSZ) syscall, no translation of any kind needed
 * -- TIOCGWINSZ's numeric value (0x5413) and struct winsize's layout
 * (ws_row/ws_col/ws_xpixel/ws_ypixel, four unsigned shorts) are already
 * ntlibc's own <sys/ioctl.h> values, and those already match the real
 * Linux kernel ABI bit-for-bit (confirmed against this host's own
 * <asm-generic/ioctls.h>/<asm-generic/termios.h>), the same "no
 * translation needed" situation __plat_fionread_pipe()'s own FIONREAD
 * call already documents above. No fd-type check of any kind: src/ioctl/
 * ioctl.c's own TIOCGWINSZ case calls this for ANY fd (Linux folds every
 * character device into __FD_CHAR -- see that file's updated banner),
 * so the real ioctl(2) call itself, via the kernel's own ioctl_tty(2)
 * dispatch, is what decides ENOTTY vs success; this function does not
 * pre-judge that.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "plat_ioctl.h"

/* aarch64 Linux syscall numbers -- see plat_mem.c's banner for why
 * these are hardcoded rather than pulled from a host header. */
#define SYS_ioctl 29
#define SYS_statx 291
#define SYS_lseek 62

#define AT_EMPTY_PATH_LX     0x1000
#define STATX_BASIC_STATS_LX 0x7ff

/* A minimal 6-argument raw syscall: `svc #0` directly, no host libc in
 * the call path at all -- NOT `extern long syscall(long, ...)`, which
 * is satisfied by the HOST's real glibc at link time in a non-
 * freestanding build and collapses every failure to exactly -1 with
 * glibc's OWN errno rather than the raw kernel -errno this file's
 * is_sys_error()/`errno = (int)-ret` translation requires -- see
 * src/mman/linux/plat_mem.c's fix (commit 299458a) for the fuller
 * account, confirmed independently across six other Linux backends.
 * aarch64's syscall calling convention: x8 = syscall number, x0..x5 =
 * up to 6 arguments, result (or -errno in [-4095,-1]) in x0. */
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6) // NOLINT(bugprone-easily-swappable-parameters) -- raw syscall ABI slots are positional and semantically distinct
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a1;
	register long x1 __asm__("x1") = a2;
	register long x2 __asm__("x2") = a3;
	register long x3 __asm__("x3") = a4;
	register long x4 __asm__("x4") = a5;
	register long x5 __asm__("x5") = a6;
	__asm__ volatile("svc #0"
		: "+r"(x0)
		: "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
		: "memory", "cc");
	return x0;
}

static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

static int unbox(__plat_handle_t h)
{
	return (int)((long)h - 1);
}

/* struct statx_timestamp / struct statx, the kernel's own fixed ABI
 * shape (linux/stat.h) -- offsets confirmed against this host's real
 * header via offsetof()/sizeof(), not assumed:
 *   sizeof(struct statx) == 256, stx_size at offset 40.
 * Only the fields this file actually reads are named individually;
 * everything else is absorbed into the trailing __spare padding so the
 * kernel always has at least as much room as it expects regardless of
 * which fields a future kernel adds within that reserved tail. */
struct __lx_statx_timestamp { // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling mirrors the Linux kernel ABI layout
	long long tv_sec;
	unsigned int tv_nsec;
	int __reserved; // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling mirrors the Linux kernel ABI layout
};

struct __lx_statx { // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling mirrors the Linux kernel ABI layout
	unsigned int stx_mask;
	unsigned int stx_blksize;
	unsigned long long stx_attributes;
	unsigned int stx_nlink;
	unsigned int stx_uid;
	unsigned int stx_gid;
	unsigned short stx_mode;
	unsigned short __spare0[1]; // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling mirrors the Linux kernel ABI layout
	unsigned long long stx_ino;
	unsigned long long stx_size;
	unsigned long long stx_blocks;
	unsigned long long stx_attributes_mask;
	struct __lx_statx_timestamp stx_atime;
	struct __lx_statx_timestamp stx_btime;
	struct __lx_statx_timestamp stx_ctime;
	struct __lx_statx_timestamp stx_mtime;
	unsigned int stx_rdev_major;
	unsigned int stx_rdev_minor;
	unsigned int stx_dev_major;
	unsigned int stx_dev_minor;
	unsigned long long stx_mnt_id;
	unsigned int stx_dio_mem_align;
	unsigned int stx_dio_offset_align;
	unsigned long long __spare3[12]; // NOLINT(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp) -- spelling mirrors the Linux kernel ABI layout
};

int __plat_fionread_pipe(__plat_handle_t h, int *out)
{
	int n = 0;
	long ret = raw_syscall(SYS_ioctl, (long)unbox(h), (long)FIONREAD, (long)&n, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	*out = n;
	return 0;
}

int __plat_file_eof_and_pos(__plat_handle_t h, long long *eof, long long *pos) // NOLINT(bugprone-easily-swappable-parameters) -- fixed platform-backend contract; EOF and position outputs have distinct roles
{
	int fd = unbox(h);
	struct __lx_statx stx;
	long ret;

	memset(&stx, 0, sizeof stx);
	ret = raw_syscall(SYS_statx, (long)fd, (long)"", (long)AT_EMPTY_PATH_LX,
	                 (long)STATX_BASIC_STATS_LX, (long)&stx, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }

	ret = raw_syscall(SYS_lseek, (long)fd, 0L, 1L /* SEEK_CUR */, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }

	*eof = (long long)stx.stx_size;
	*pos = ret;
	return 0;
}

int __plat_tiocgwinsz(__plat_handle_t h, struct winsize *ws)
{
	long ret = raw_syscall(SYS_ioctl, (long)unbox(h), (long)TIOCGWINSZ, (long)ws, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

// NOLINTEND(misc-include-cleaner)
