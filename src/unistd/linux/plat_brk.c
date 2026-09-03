/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * brk()/sbrk() for native Linux: a real program-break, via the real
 * brk(2) syscall. include/unistd.h's own "undefined-ok" comment on
 * these two says NT's allocator (RtlAllocateHeap, src/malloc/malloc.c)
 * is not "a single growable brk-style arena" and NT has no primitive
 * shaped like brk() at all -- true, and that reasoning stays for NT
 * (same NT-reasoning-stays-Linux-gets-real-code split as this tree's
 * own syscall()/setresuid()/euidaccess()/syncfs() precedent).
 *
 * Genuinely independent of ntlibc's own malloc(): src/malloc/linux/
 * plat_malloc.c's __plat_alloc() is built entirely on raw mmap(2)/
 * munmap(2) -- checked before writing this file, not assumed -- and
 * never calls brk(2) or touches the traditional data-segment program
 * break at all. musl and glibc both ship a real, independent brk()/
 * sbrk() pair on Linux for the identical reason: the traditional break
 * and a modern mmap-based allocator are two separate address-space
 * regions that do not interact, so a caller using brk()/sbrk() directly
 * (rare today, but a real POSIX/BSD API real programs still call)
 * cannot corrupt, or be corrupted by, this library's own malloc() no
 * matter how either one is implemented internally.
 *
 * Linux's raw brk(2) syscall does not use the usual [-4095,-1] "-errno"
 * failure convention every OTHER syscall wrapper in this tree's Linux
 * backends checks with an is_sys_error() helper: it always returns the
 * resulting break address, moved or not (glibc's and musl's own brk(2)
 * wrappers both use exactly the "did the break actually move" test
 * below, not an error code) -- so, uniquely among this tree's Linux
 * backends, this file has no is_sys_error() at all.
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
 * banner for the fuller per-arch rationale (this file only ever needs
 * one argument, but keeps the same six-slot shape every other Linux
 * backend's own raw_syscall() uses, for the identical "one syscall
 * table per file" discipline). */
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
	uintptr_t base;
	init_brk();
	if (increment == 0) return cur_brk;
	oldbrk = cur_brk;
	/* Same direction-aware overflow check glibc's own sbrk() uses, but
	 * done in uintptr_t rather than on the pointers directly: comparing
	 * pointers formed by an out-of-bounds addition is undefined
	 * behaviour (C99 6.5.6p8), and an optimizer is entitled to assume
	 * that overflow never happens and delete the check. Casting a
	 * signed increment to uintptr_t wraps modulo 2^N (C99 6.3.1.3p2),
	 * matching the pointer arithmetic bit-for-bit while staying inside
	 * defined behaviour throughout. */
	base = (uintptr_t)oldbrk;
	if (increment > 0) {
		if (base + (uintptr_t)increment < base) { errno = ENOMEM; return (void *)-1; }
	} else {
		if (base + (uintptr_t)increment > base) { errno = ENOMEM; return (void *)-1; }
	}
	if (brk(oldbrk + increment) < 0) return (void *)-1;
	return oldbrk;
}

// NOLINTEND(misc-include-cleaner)
