/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A real allocator for Linux, in the sense src/malloc/nt/plat_malloc.c
 * does not need one: NT has RtlAllocateHeap, a serious, already-
 * written allocator with size classes, coalescing and low-fragmentation
 * behaviour, sitting right there in every process. Linux has mmap(2)
 * and nothing else at this layer, so this file is the allocator, not
 * a wrapper around one.
 *
 * Deliberately NOT a general-purpose, competitively-tuned allocator
 * (no coalescing of adjacent free chunks, no arena reclamation, no
 * thread-local caches): a segregated free-list design, one free list
 * per power-of-two size class, backed by mmap'd slabs that are carved
 * up and never returned to the kernel. What this buys, and why it is
 * the right tradeoff for this port specifically: no boundary-tag
 * bookkeeping and no merge logic at all, which is where a hand-rolled
 * allocator most often gets subtly wrong in exactly the way that is
 * hardest to catch (silent heap corruption that only crashes some
 * unrelated allocation much later) -- see this file's own test for
 * how that risk was actually checked, not just reasoned about. The
 * cost is real but bounded: a size class never shrinks once grown, so
 * a program with one large allocation burst and a long quiet tail
 * keeps that memory mapped for its own lifetime. Acceptable for a
 * conformance-suite-shaped workload; a real tuned allocator is
 * separate, future work if this port ever needs one.
 *
 * Layout: every live allocation is preceded by a 16-byte header
 * (struct chunk_hdr) holding its own usable size and which free list
 * it belongs to (or -1 for a "large" allocation, mmap'd and munmap'd
 * directly, one mapping per allocation, no size class involved). 16
 * bytes keeps the header itself, and therefore the user pointer right
 * after it, a multiple of 16 as long as every class size and the slab
 * size are too -- the same alignment src/malloc/nt/plat_malloc.c's own
 * banner documents NT's heap already gives on x86_64.
 *
 * Thread safety: __plat_fast_lock()/__plat_fast_unlock() (src/internal/
 * plat_thread.h) guard every free-list read/write. No thread is
 * actually spawned by anything on this platform yet (pthread_create()
 * has no Linux backend -- a separate, disclosed gap), but a future one
 * would corrupt these lists instantly without this, and the lock costs
 * nothing observable today.
 */
#include "plat_malloc.h"
#include "plat_thread.h"

/* __builtin_memset/__builtin_memcpy, not <string.h>'s memset()/
 * memcpy(): this file sits directly below malloc()/calloc()/realloc()
 * in the call graph, and reaching for this project's own memcpy.c/
 * memset.c definitions here is exactly the kind of "which came first"
 * dependency this backend has no reason to create when the compiler
 * builtins do the same job without it, real functions or not. */

#define SYS_mmap 222
#define SYS_munmap 215

#define PAGE_SIZE 4096u
#define SLAB_BYTES (64u * 1024u)
#define HDR_SIZE 16u
#define NUM_CLASSES 12 /* 16, 32, 64, ..., 16 << 11 = 32768 */

#define PROT_READ_LX 0x1
#define PROT_WRITE_LX 0x2
#define MAP_PRIVATE_LX 0x02
#define MAP_ANONYMOUS_LX 0x20

/* Same raw syscall trampoline every Linux backend in this tree defines
 * for itself -- see src/mman/linux/plat_mem.c's banner for why this is
 * never `extern long syscall(long, ...)` (resolves against the HOST's
 * glibc at link time, collapsing every failure's errno to the wrong
 * value). */
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

static void *raw_mmap(size_t len)
{
	long ret = raw_syscall(SYS_mmap, 0, (long)len, PROT_READ_LX | PROT_WRITE_LX,
	                       MAP_PRIVATE_LX | MAP_ANONYMOUS_LX, -1L, 0L);
	if (is_sys_error(ret)) return 0;
	return (void *)ret;
}

static void raw_munmap(void *addr, size_t len)
{
	raw_syscall(SYS_munmap, (long)addr, (long)len, 0, 0, 0, 0);
}

struct chunk_hdr {
	size_t size;  /* usable bytes available to the caller */
	long class;   /* index into free_list[], or -1 for a large (direct
	               * mmap'd, one mapping per allocation) chunk */
};

static void *free_list[NUM_CLASSES];

static size_t class_size(int class) { return (size_t)HDR_SIZE << class; }

static int class_for(size_t n)
{
	int class;
	size_t sz = HDR_SIZE;
	for (class = 0; class < NUM_CLASSES; class++, sz <<= 1)
		if (sz >= n) return class;
	return -1; /* too big for any class -- large path */
}

static size_t roundup_page(size_t n)
{
	return (n + (PAGE_SIZE - 1)) & ~(size_t)(PAGE_SIZE - 1);
}

/* Refill free_list[class] with one freshly mmap'd slab's worth of
 * chunks. Called with the lock held (matching every other free_list
 * access), released around the mmap() itself: a page-fault-triggering
 * kernel round trip is not the kind of thing this process-wide lock
 * should be held across, and nothing here needs it to be -- worst
 * case two threads both refill the same class at once and the second
 * one's extra chunks simply also go on the list. */
static void refill_locked(int class)
{
	size_t csz = class_size(class);
	size_t stride = HDR_SIZE + csz;
	size_t n = SLAB_BYTES / stride;
	unsigned char *slab;
	size_t i;

	__plat_fast_unlock();
	slab = raw_mmap(SLAB_BYTES);
	__plat_fast_lock();
	if (!slab) return;

	for (i = 0; i < n; i++) {
		struct chunk_hdr *h = (struct chunk_hdr *)(slab + i * stride);
		void *user = (unsigned char *)h + HDR_SIZE;
		h->size = csz;
		h->class = class;
		*(void **)user = free_list[class];
		free_list[class] = user;
	}
}

void *__plat_alloc(size_t n, int zero)
{
	struct chunk_hdr *h;
	void *user;
	int class;

	if (n == 0) n = 1;

	if (n > class_size(NUM_CLASSES - 1)) {
		/* Large path: one mapping, no free list involved. */
		size_t total = roundup_page(HDR_SIZE + n);
		unsigned char *base = raw_mmap(total);
		if (!base) return 0;
		h = (struct chunk_hdr *)base;
		h->size = n;
		h->class = -1;
		user = base + HDR_SIZE;
		/* mmap'd pages are already zero -- memset anyway rather than
		 * trust that invariant silently at every call site; see this
		 * file's own banner on correctness over micro-optimisation. */
		if (zero) __builtin_memset(user, 0, n);
		return user;
	}

	class = class_for(n);
	__plat_fast_lock();
	if (!free_list[class]) refill_locked(class);
	if (!free_list[class]) { __plat_fast_unlock(); return 0; }
	user = free_list[class];
	free_list[class] = *(void **)user;
	__plat_fast_unlock();

	h = (struct chunk_hdr *)((unsigned char *)user - HDR_SIZE);
	if (zero) __builtin_memset(user, 0, h->size);
	return user;
}

size_t __plat_alloc_size(void *p)
{
	struct chunk_hdr *h = (struct chunk_hdr *)((unsigned char *)p - HDR_SIZE);
	return h->size;
}

void __plat_dealloc(void *p)
{
	struct chunk_hdr *h;
	if (!p) return;
	h = (struct chunk_hdr *)((unsigned char *)p - HDR_SIZE);
	if (h->class < 0) {
		raw_munmap(h, roundup_page(HDR_SIZE + h->size));
		return;
	}
	__plat_fast_lock();
	*(void **)p = free_list[h->class];
	free_list[h->class] = p;
	__plat_fast_unlock();
}

/* No in-place growth/shrink attempted (a free-list-of-classes design
 * has no adjacent-chunk bookkeeping to check for room to extend into,
 * on purpose -- see this file's own banner): always a fresh
 * allocation, a copy of the smaller of the two sizes, and a free of
 * the original. Simple, and the copy is bounded by real, exact sizes
 * both ends already track precisely, so there is no scope for an
 * over-read here even though it is not the fastest possible realloc. */
void *__plat_realloc(void *p, size_t n)
{
	struct chunk_hdr *h;
	void *q;
	size_t old;

	if (!p) return __plat_alloc(n, 0);
	h = (struct chunk_hdr *)((unsigned char *)p - HDR_SIZE);
	old = h->size;
	if (n == 0) { __plat_dealloc(p); return 0; }
	q = __plat_alloc(n, 0);
	if (!q) return 0;
	__builtin_memcpy(q, p, old < n ? old : n);
	__plat_dealloc(p);
	return q;
}
