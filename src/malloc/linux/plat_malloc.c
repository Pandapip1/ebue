/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux's half of src/internal/plat_malloc_generic.h's split: the one
 * thing this platform can supply that the generic allocator cannot --
 * raw, page-granular anonymous memory, via mmap(2)/munmap(2) directly
 * (never the host's syscall(2) wrapper; see src/mman/linux/plat_mem.c's
 * banner for why that collapses every failure's errno to the wrong
 * value). Everything else -- size classes, free lists, the allocation
 * algorithm itself -- lives in that header, written once so a future
 * platform in the same position (no serious heap manager of its own to
 * delegate to, the way src/malloc/nt/plat_malloc.c can) only needs to
 * supply this same small pair of functions, not rewrite the allocator.
 */
#include "plat_pages.h"

#define SYS_mmap 222
#define SYS_munmap 215

#define PROT_READ_LX 0x1
#define PROT_WRITE_LX 0x2
#define MAP_PRIVATE_LX 0x02
#define MAP_ANONYMOUS_LX 0x20

/* Same raw syscall trampoline every Linux backend in this tree defines
 * for itself -- see src/mman/linux/plat_mem.c's banner for why this is
 * never `extern long syscall(long, ...)` (resolves against the HOST's
 * glibc at link time). */
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

void *__plat_pages_alloc(size_t n)
{
	long ret = raw_syscall(SYS_mmap, 0, (long)n, PROT_READ_LX | PROT_WRITE_LX,
	                       MAP_PRIVATE_LX | MAP_ANONYMOUS_LX, -1L, 0L);
	if (is_sys_error(ret)) return 0;
	return (void *)ret; /* MAP_ANONYMOUS is always zero-filled by the kernel */
}

void __plat_pages_free(void *p, size_t n)
{
	raw_syscall(SYS_munmap, (long)p, (long)n, 0, 0, 0, 0);
}

#include "plat_malloc_generic.h"
