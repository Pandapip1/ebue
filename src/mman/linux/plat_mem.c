/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_mem.h -- the second real
 * backend behind the platform-abstraction seam the NT pilot (f4b5c99)
 * introduced, proving the interface against an OS whose memory-mapping
 * primitives look nothing like NT's.
 *
 * Every call here is a single raw Linux syscall, issued directly via
 * syscall(2) rather than through any host libc wrapper -- ntlibc talks
 * to the kernel itself, the same discipline the NT side keeps toward
 * ntdll, and the same technique fuzz/ntstubs.c already uses for this
 * exact reason ("those names belong to ntlibc in this link"). This file
 * is compiled under -nostdinc against ntlibc's OWN generated headers
 * (obj/include/bits/alltypes.h, include/), never glibc's, so it cannot
 * include a host header for the syscall numbers or wrapper prototype
 * either -- both are declared/defined locally below.
 *
 * NT's reserve/commit split, and the section-view-vs-anonymous-
 * reservation distinction mman.c's own banner describes at length, do
 * not exist on Linux at all: mmap()/munmap() are already page-granular
 * end to end, and every one of __plat_mem_decommit()/_release()/
 * _unmap_view() below is the identical munmap() call -- exactly what
 * that banner predicted a backend like this would look like ("a
 * backend whose native mmap/munmap are page-granular end to end would
 * not need this reservation-table strategy at all"). Likewise
 * __plat_mem_map_file() needs none of the NT backend's EOF-capture/
 * zero-fill-tail workaround (Linux's mmap already zero-fills the
 * partial page past a file's real end, correctly, natively) or its
 * read-only-section fallback dance (a Linux mmap() against a
 * insufficiently-open fd simply fails EACCES outright), and
 * __plat_mem_flush_view() needs no separate timestamp fix-up after
 * msync() (Linux updates the file's mtime as a normal side effect of
 * writing the dirty pages back) -- see plat_mem.h's own comment
 * anticipating exactly this backend for both of those simplifications.
 */
#include <sys/mman.h>
#include <errno.h>
#include "plat_mem.h"

/* aarch64 Linux syscall numbers (arch/arm64/include/uapi/asm/unistd.h,
 * via the generic modern ABI's asm-generic/unistd.h) -- confirmed
 * against this host's own <sys/syscall.h> rather than assumed, since
 * they are architecture-specific and this file's -nostdinc build
 * cannot include that header itself (it would pull in glibc's
 * conflicting type system alongside ntlibc's own). */
#define SYS_mmap     222
#define SYS_munmap   215
#define SYS_mprotect 226
#define SYS_msync    227
#define SYS_mlock    228
#define SYS_munlock  229

/* A minimal 6-argument raw syscall: `svc #0` directly, no host libc in
 * the call path at all. NOT `extern long syscall(long, ...)`: that
 * symbol is satisfied by the HOST's real glibc at link time (this
 * build is -nostdinc, not -nostdlib -- only compiling avoids the host
 * headers, the final link step still pulls in host libc), and glibc's
 * syscall() performs its own error translation: on failure it returns
 * exactly -1 and sets glibc's OWN errno (a different memory location
 * than ntlibc's own errno global, src/internal/errno.c) to the real
 * code -- it does NOT hand back the raw kernel -errno in [-4095,-1]
 * this file's is_sys_error()/`errno = (int)-ret` translation requires.
 * Confirmed both by inspecting the linked pilot binary (nm -D shows an
 * undefined `syscall@GLIBC_*`, resolved by ld-linux at runtime) and
 * independently by src/thread/linux/plat_thread.c's own port, which
 * hit the identical bug and is this fix's model. aarch64's syscall
 * calling convention: x8 = syscall number, x0..x5 = up to 6 arguments,
 * result (or -errno in [-4095,-1]) in x0. */
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

/* A raw Linux syscall returns the result on success, or -errno (as an
 * unsigned value in [-4095, -1]) on failure -- the kernel's own ABI
 * convention on every architecture this targets. mmap()'s successful
 * return is a mapped address, which can itself be a huge unsigned
 * value without being an error; the [-4095,-1] window is what
 * distinguishes the two. */
static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

int __plat_mem_reserve(void **base_inout, size_t len, int prot)
{
	/* The PROT_ and MAP_ constants are ntlibc's own <sys/mman.h>
	 * values, unchanged: they already match the Linux kernel ABI
	 * exactly (confirmed by reading include/sys/mman.h), so unlike
	 * the NT backend's prot_to_page()/prot_to_view() this file needs no
	 * translation table anywhere. */
	long ret = raw_syscall(SYS_mmap, (long)*base_inout, (long)len, (long)prot,
	                       (long)(MAP_PRIVATE | MAP_ANONYMOUS), -1L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	*base_inout = (void *)ret;
	return 0;
}

int __plat_mem_commit_fixed(void *base, size_t len, int prot)
{
	/* A single MAP_FIXED anonymous mmap() over an address range this
	 * process already reserved atomically discards whatever was there
	 * and hands back fresh zero-filled pages -- mmap.html's MAP_FIXED
	 * "discarded" requirement in one syscall, where the NT backend
	 * needs an explicit decommit-then-commit pair (see its own
	 * comment) because a bare NT commit over already-committed pages
	 * would leave the old bytes in place. */
	long ret = raw_syscall(SYS_mmap, (long)base, (long)len, (long)prot,
	                       (long)(MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED), -1L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_mem_decommit(void *base, size_t len)
{
	long ret = raw_syscall(SYS_munmap, (long)base, (long)len, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_mem_release(void *base, size_t len)
{
	long ret = raw_syscall(SYS_munmap, (long)base, (long)len, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_mem_protect(void *addr, size_t len, int prot)
{
	long ret = raw_syscall(SYS_mprotect, (long)addr, (long)len, (long)prot, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_mem_lock(void *addr, size_t len)
{
	long ret = raw_syscall(SYS_mlock, (long)addr, (long)len, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_mem_unlock(void *addr, size_t len)
{
	long ret = raw_syscall(SYS_munlock, (long)addr, (long)len, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_mem_map_file(__plat_handle_t fh, int prot, int flags, off_t off,
                        size_t viewbytes, void **base_inout)
{
	/* fh is this backend's own boxed fd (see src/unistd/linux/plat_fd.c's
	 * banner for the encoding); unbox it the same way that file does. */
	int fd = (int)((long)fh - 1);
	long ret = raw_syscall(SYS_mmap, (long)*base_inout, (long)viewbytes, (long)prot,
	                       (long)((flags & MAP_SHARED) ? MAP_SHARED : MAP_PRIVATE),
	                       (long)fd, (long)off);
	if (is_sys_error(ret)) {
		/* mmap.html has no broader vocabulary for a failed file-backed
		 * mapping than ENOMEM/ENOTSUP -- the same narrowing the NT
		 * backend does, for the same reason (see plat_mem.h). */
		errno = (-ret == ENOMEM) ? ENOMEM : ENOTSUP;
		return -1;
	}
	*base_inout = (void *)ret;
	return 0;
}

int __plat_mem_unmap_view(void *base, size_t len)
{
	long ret = raw_syscall(SYS_munmap, (long)base, (long)len, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

int __plat_mem_flush_view(void *addr, size_t len, __plat_handle_t writeback)
{
	long ret;
	(void)writeback; /* Linux's msync() needs no separate handle: it
	                  * operates purely on the mapped address range, and
	                  * already updates the file's mtime as a normal
	                  * side effect -- see this file's own banner. */
	ret = raw_syscall(SYS_msync, (long)addr, (long)len, (long)MS_SYNC, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}
