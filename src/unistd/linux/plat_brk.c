/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * brk()/sbrk() for native Linux: a real program-break, via the real
 * brk(2) syscall. NT has no primitive shaped like brk() at all, so that
 * reasoning stays NT-only; Linux gets real code.
 *
 * Genuinely independent of ntlibc's own malloc(): src/malloc/linux/
 * plat_malloc.c's __plat_alloc() is built entirely on raw mmap(2)/
 * munmap(2) and never touches the traditional data-segment break, so a
 * caller using brk()/sbrk() directly cannot corrupt, or be corrupted by,
 * this library's own malloc().
 *
 * Linux's raw brk(2) syscall does not use the usual [-4095,-1] "-errno"
 * failure convention every other syscall wrapper in this tree checks with
 * is_sys_error(): it always returns the resulting break address, moved or
 * not, so uniquely among this tree's Linux backends this file has no
 * is_sys_error() at all.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <unistd.h>
#include <stdint.h>
#include <errno.h>

/* aarch64 Linux syscall number -- confirmed against this host's own
 * <asm-generic/unistd.h>, not assumed. */
#define SYS_brk 214

/* A minimal 6-argument raw syscall -- see src/mman/linux/plat_mem.c's
 * banner; this file only ever needs one argument but keeps the same
 * six-slot shape every other Linux backend uses. */
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

static void *raw_brk(void *addr)
{
	return (void *)raw_syscall(SYS_brk, (long)addr, 0L, 0L, 0L, 0L, 0L);
}

/* The kernel's own idea of the break, queried once via brk(0) -- a pure
 * query on this syscall's own contract, since passing an address at or
 * below the current break (which 0 always is, on a real Linux process)
 * never moves anything -- and cached afterward, exactly the way
 * glibc's __curbrk and musl's equivalent single-threaded cache both
 * work. */
static void *cur_brk;
static int have_brk;

static void init_brk(void)
{
	if (!have_brk) {
		cur_brk = raw_brk(0);
		have_brk = 1;
	}
}

int brk(void *addr)
{
	void *nb;
	init_brk();
	nb = raw_brk(addr);
	cur_brk = nb;
	/* brk(2)'s own contract: returns the new break on success, the
	 * unchanged (old) break on failure -- "did it actually reach (at
	 * least) where we asked" is the only failure signal there is. */
	if ((uintptr_t)nb < (uintptr_t)addr) { errno = ENOMEM; return -1; }
	return 0;
}

void *sbrk(intptr_t increment)
{
	char *oldbrk;
	init_brk();
	if (increment == 0) return cur_brk;
	oldbrk = cur_brk;
	/* Overflow check on the pointer arithmetic itself, the same
	 * direction-aware test glibc's own sbrk() uses. */
	if (increment > 0) {
		if (oldbrk + increment < oldbrk) { errno = ENOMEM; return (void *)-1; }
	} else {
		if (oldbrk + increment > oldbrk) { errno = ENOMEM; return (void *)-1; }
	}
	if (brk(oldbrk + increment) < 0) return (void *)-1;
	return oldbrk;
}

// NOLINTEND(misc-include-cleaner)
