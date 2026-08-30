/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux's __fd_init(), replacing src/internal/fd.c's own (PLATFORM's
 * REPLACED_OBJS override, same mechanism crt/linux/crt1.c uses for
 * crt/crt1.c -- see Makefile's PLAT_GLOBS comment).
 *
 * NT's __fd_init() (fd.c) reads __peb->ProcessParameters for the three
 * standard handles and an optional RuntimeData blob describing every
 * OTHER descriptor a parent chose to hand down across CreateProcess --
 * both concepts specific to how NT starts a process; there is no PEB,
 * no ProcessParameters, and no such inheritance blob on Linux. A real
 * Linux process instead simply already HAS descriptors 0/1/2 open (a
 * shell or exec() sets them up before this program's first instruction
 * ever runs) and inherits every other still-open, non-close-on-exec
 * descriptor automatically, with the kernel itself as the only
 * bookkeeping authority -- ntlibc's own __fds[] table here only needs
 * to learn about the three standard ones; every other inherited fd
 * remains perfectly usable at the raw-syscall level even before this
 * library's table knows about it (exactly like a descriptor a Linux
 * program opens with a raw syscall of its own, bypassing this library
 * entirely, already works today).
 *
 * Classification (below, shared between __fd_init() and this file's
 * own __handle_type()) reuses src/fcntl/linux/plat_fcntl.c's own
 * statx()-based approach and constants (see that file's __plat_open()
 * for the fuller rationale). __handle_type() itself is declared in
 * src/internal/libc.h and called from a few genuinely portable front
 * doors (src/fcntl/fadvise.c, src/select/select.c) that never see the
 * NT-vs-Linux split directly -- this is that split's Linux half, the
 * same way src/internal/nt/plat_fd_init.c's is the NT half (a raw
 * NtQueryVolumeInformationFile/NtQueryInformationFile pair there,
 * where here a single statx(2) answers the same question). */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include "libc.h"

#define SYS_statx 291

#define AT_EMPTY_PATH_LX     0x1000
#define STATX_BASIC_STATS_LX 0x7ff

#define S_IFMT_LX   0170000
#define S_IFSOCK_LX 0140000
#define S_IFDIR_LX  0040000
#define S_IFCHR_LX  0020000
#define S_IFIFO_LX  0010000

/* Same 6-argument raw syscall trampoline every Linux backend defines
 * for itself (never the host's syscall(2) wrapper -- see plat_mem.c's
 * banner for why that collapses every failure's errno to the wrong
 * value). File-scoped by convention, not shared, the same as every
 * other Linux backend in this tree. */
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
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

static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

struct __lx_statx_timestamp {
	long long tv_sec;
	unsigned int tv_nsec;
	int __reserved;
};
struct __lx_statx {
	unsigned int stx_mask;
	unsigned int stx_blksize;
	unsigned long long stx_attributes;
	unsigned int stx_nlink;
	unsigned int stx_uid;
	unsigned int stx_gid;
	unsigned short stx_mode;
	unsigned short __spare0[1];
	unsigned long long stx_ino;
	unsigned long long stx_size;
	unsigned long long stx_blocks;
	unsigned long long __rest[26];
};

/* Returns __FD_UNKNOWN (never 0) on a closed/invalid fd, matching
 * __handle_type()'s own NT contract -- 0 is not a member of the __FD_*
 * enum (src/internal/libc.h: __FD_FILE starts at 1), so a caller like
 * src/internal/fd.c's __fd_install_at() (`type ? type : __handle_type(h)`)
 * cannot mistake this for "no override, fall through". */
static int classify_fd(int fd)
{
	struct __lx_statx stx;
	long ret;

	memset(&stx, 0, sizeof stx);
	ret = raw_syscall(SYS_statx, (long)fd, (long)"", (long)AT_EMPTY_PATH_LX,
	                  (long)STATX_BASIC_STATS_LX, (long)&stx, 0L);
	if (is_sys_error(ret)) return __FD_UNKNOWN;

	switch (stx.stx_mode & S_IFMT_LX) {
	case S_IFDIR_LX:  return __FD_DIR;
	case S_IFIFO_LX:  return __FD_PIPE;
	case S_IFCHR_LX:  return __FD_CHAR;
	case S_IFSOCK_LX: return __FD_SOCKET;
	default:          return __FD_FILE;
	}
}

/* HANDLE here is always this backend's own boxed (fd + 1) encoding
 * (src/unistd/linux/plat_fd.c's banner) -- every caller of this
 * function already holds a __plat_handle_t this backend itself
 * produced, never a raw platform object the way NT's version queries
 * one. */
int __handle_type(HANDLE h)
{
	long fd = (long)h - 1;
	if (fd < 0) return __FD_UNKNOWN;
	return classify_fd((int)fd);
}

static void install_std(int fd)
{
	int type = classify_fd(fd);
	if (type == __FD_UNKNOWN) return; /* fd not actually open -- leave the slot empty */

	/* Boxed (fd + 1), same convention src/unistd/linux/plat_fd.c's own
	 * banner documents (__PLAT_HANDLE_NULL is 0, and fd 0/stdin is a
	 * real, valid descriptor that must not collide with "empty slot").
	 * O_RDONLY for fd 0, O_WRONLY for 1/2: the same asymmetry NT's
	 * install_std() encodes, and for the identical reason -- write()
	 * refuses an O_RDONLY descriptor and O_RDONLY is 0, so getting this
	 * wrong for an inherited stdout/stderr would silently break writes
	 * to it. */
	__fd_install_at(fd, (HANDLE)(long)(fd + 1), fd == 0 ? O_RDONLY : O_WRONLY, type);
}

void __fd_init(void)
{
	install_std(0);
	install_std(1);
	install_std(2);
}
