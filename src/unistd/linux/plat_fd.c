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

/* Linux syscall numbers -- see plat_mem.c's banner for why these are
 * hardcoded rather than pulled from a host header; x86_64/i386 numbers
 * confirmed against arch/x86/entry/syscalls/syscall_{64,32}.tbl. i386
 * has no plain SYS_dup argument-compatible... it does (dup takes one
 * fd, same as every arch) -- only pread64/pwrite64 differ in ARGUMENT
 * SHAPE on i386 (see __plat_pread()/__plat_pwrite() below), not in
 * having a syscall number at all. */
#if defined(__aarch64__)
#define SYS_close  57
#define SYS_lseek  62
#define SYS_read   63
#define SYS_write  64
#define SYS_pread64  67
#define SYS_pwrite64 68
#define SYS_dup    23
#define SYS_fcntl  25
#elif defined(__x86_64__)
#define SYS_close  3
#define SYS_lseek  8
#define SYS_read   0
#define SYS_write  1
#define SYS_pread64  17
#define SYS_pwrite64 18
#define SYS_dup    32
#define SYS_fcntl  72
#elif defined(__i386__)
#define SYS_close  6
/* Plain lseek(2) (19), not _llseek(140): i386's lseek(2) takes and
 * returns a 32-bit offset directly (no pointer-based 64-bit result the
 * way _llseek needs) -- a real, disclosed limitation matching this
 * project's own documentation culture, not a silent gap: seeking past
 * 2 GiB in a single file fails/wraps on THIS backend's __plat_seek_*()
 * below on i386 specifically, unlike aarch64/x86_64 where lseek(2)
 * already takes a native 64-bit offset. Fine for what this pass proves
 * (the CRT/dlopen pilots below never seek at all); a real fix is
 * _llseek(2), separate future work should a 32-bit-Linux consumer ever
 * need large-file seeks. */
#define SYS_lseek  19
#define SYS_read   3
#define SYS_write  4
#define SYS_pread64  180
#define SYS_pwrite64 181
#define SYS_dup    41
#define SYS_fcntl  55
#else
#error "plat_fd.c: unsupported architecture"
#endif

#define F_SETFD_LX   2
#define FD_CLOEXEC_LX 1

/* A minimal 6-argument raw syscall, one body per arch's own calling
 * convention -- no host libc in the call path at all. NOT `extern long
 * syscall(long, ...)`: that symbol is satisfied by the HOST's real
 * glibc at link time (this build is -nostdinc, not -nostdlib -- only
 * compiling avoids the host headers, the final link step still pulls
 * in host libc), and glibc's syscall() performs its own error
 * translation: on failure it returns exactly -1 and sets glibc's OWN
 * errno (a different memory location than ntlibc's own errno global,
 * src/internal/errno.c) to the real code -- it does NOT hand back the
 * raw kernel -errno in [-4095,-1] this file's is_sys_error()/`errno =
 * (int)-ret` translation requires. Confirmed both by inspecting the
 * linked pilot binary (nm -D shows an undefined `syscall@GLIBC_*`,
 * resolved by ld-linux at runtime) and independently by src/thread/
 * linux/plat_thread.c's own port, which hit the identical bug and is
 * this fix's model. See crt/linux/crt1.c's own raw_syscall() banner
 * for the fuller per-arch calling-convention rationale (x86_64's
 * syscall/rdi.../r10 shape, i386's int $0x80 + memory-array-of-args
 * shape for its one 6-argument syscall this file also needs -- pread64/
 * pwrite64 pass a 64-bit offset, so i386's version below needs a 7-word
 * array, not the 6-argument crt1.c uses for its own single mmap2 call). */
#if defined(__aarch64__)
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
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
#elif defined(__x86_64__)
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
	long ret;
	register long r10 __asm__("r10") = a4;
	register long r8  __asm__("r8")  = a5;
	register long r9  __asm__("r9")  = a6;
	__asm__ volatile("syscall"
	                 : "=a"(ret)
	                 : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
	                 : "rcx", "r11", "memory");
	return ret;
}
#elif defined(__i386__)
/* Same "point eax at an explicit args array" technique as crt/linux/
 * crt1.c's own i386 raw_syscall() -- see that file's banner for the
 * full rationale (ebp is both cdecl's frame-pointer register and the
 * only place left to put a 6th argument). Duplicated here per this
 * tree's own "own syscall table per file" discipline, not shared. */
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
	long args[7];
	long ret;
	args[0] = nr; args[1] = a1; args[2] = a2; args[3] = a3;
	args[4] = a4; args[5] = a5; args[6] = a6;
	__asm__ volatile(
		"pushl %%ebp\n\t"
		"pushl %%ebx\n\t"
		"movl 4(%%eax), %%ebx\n\t"
		"movl 8(%%eax), %%ecx\n\t"
		"movl 12(%%eax), %%edx\n\t"
		"movl 16(%%eax), %%esi\n\t"
		"movl 20(%%eax), %%edi\n\t"
		"movl 24(%%eax), %%ebp\n\t"
		"movl (%%eax), %%eax\n\t"
		"int $0x80\n\t"
		"popl %%ebx\n\t"
		"popl %%ebp"
		: "=a"(ret)
		: "a"(args)
		: "ecx", "edx", "esi", "edi", "memory", "cc");
	return ret;
}
#endif

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
	long ret = raw_syscall(SYS_close, (long)unbox(h), 0L, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

ssize_t __plat_read(__plat_handle_t h, void *buf, size_t count)
{
	long ret = raw_syscall(SYS_read, (long)unbox(h), (long)buf, (long)count, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (ssize_t)ret;
}

ssize_t __plat_pread(__plat_handle_t h, void *buf, size_t count, off_t off)
{
	long ret;
#if defined(__i386__)
	/* i386's pread64(2) takes a 64-bit offset as two SEPARATE 32-bit
	 * argument registers (pos_low, pos_high -- confirmed against the
	 * real kernel's fs/read_write.c SYSCALL_DEFINE5(pread64, ...)
	 * signature on 32-bit longs), not one `long` the way every other
	 * arg to this trampoline works: `(long)off` alone would silently
	 * truncate off_t (always 64-bit in this project -- see include/
	 * alltypes.h.in) to its low 32 bits. Little-endian, so "low"/
	 * "high" below are the numeric halves, not a memory-layout
	 * concern. */
	unsigned long long uoff = (unsigned long long)off;
	ret = raw_syscall(SYS_pread64, (long)unbox(h), (long)buf, (long)count,
	                  (long)(unsigned long)(uoff & 0xffffffffu),
	                  (long)(unsigned long)(uoff >> 32), 0L);
#else
	ret = raw_syscall(SYS_pread64, (long)unbox(h), (long)buf, (long)count, (long)off, 0L, 0L);
#endif
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
	ret = raw_syscall(SYS_write, (long)unbox(h), (long)buf, (long)count, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (ssize_t)ret;
}

ssize_t __plat_pwrite(__plat_handle_t h, const void *buf, size_t count, off_t off)
{
	long ret;
#if defined(__i386__)
	/* See __plat_pread()'s identical comment just above -- same split. */
	unsigned long long uoff = (unsigned long long)off;
	ret = raw_syscall(SYS_pwrite64, (long)unbox(h), (long)buf, (long)count,
	                  (long)(unsigned long)(uoff & 0xffffffffu),
	                  (long)(unsigned long)(uoff >> 32), 0L);
#else
	ret = raw_syscall(SYS_pwrite64, (long)unbox(h), (long)buf, (long)count, (long)off, 0L, 0L);
#endif
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
		ret = raw_syscall(SYS_lseek, (long)fd, 0L, 1L /* SEEK_CUR */, 0L, 0L, 0L);
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
	cur = raw_syscall(SYS_lseek, (long)fd, 0L, 1L /* SEEK_CUR */, 0L, 0L, 0L);
	if (is_sys_error(cur)) { errno = (int)-cur; return -1; }
	ret = raw_syscall(SYS_lseek, (long)fd, 0L, 2L /* SEEK_END */, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	raw_syscall(SYS_lseek, (long)fd, cur, 0L /* SEEK_SET */, 0L, 0L, 0L);
	return ret;
}

int __plat_seek_set(__plat_handle_t h, long long target)
{
	long ret = raw_syscall(SYS_lseek, (long)unbox(h), (long)target, 0L /* SEEK_SET */, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_dup(__plat_handle_t h, int inheritable, __plat_handle_t *out)
{
	long newfd = raw_syscall(SYS_dup, (long)unbox(h), 0L, 0L, 0L, 0L, 0L);
	if (is_sys_error(newfd)) { errno = (int)-newfd; return -1; }
	if (!inheritable) {
		/* dup(2) never sets O_CLOEXEC on the new descriptor (matching
		 * `inheritable` true); ask for it explicitly otherwise. */
		long fc = raw_syscall(SYS_fcntl, newfd, (long)F_SETFD_LX, (long)FD_CLOEXEC_LX, 0L, 0L, 0L);
		if (is_sys_error(fc)) {
			int e = (int)-fc;
			raw_syscall(SYS_close, newfd, 0L, 0L, 0L, 0L, 0L);
			errno = e;
			return -1;
		}
	}
	*out = (__plat_handle_t)(newfd + 1);
	return 0;
}
