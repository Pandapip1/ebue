/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * A real allocator, for whichever platform backend has no serious one
 * of its own to delegate to -- the way src/malloc/nt/plat_malloc.c
 * can, by forwarding straight to ntdll's own RtlAllocateHeap (size
 * classes, a low-fragmentation front end, coalescing, guard pages
 * under the debugger, already written). A platform with nothing
 * equivalent sitting underneath it -- Linux, whose only primitive at
 * this layer is mmap(2); a future UEFI backend, whose only primitive
 * would be AllocatePages/FreePages -- needs the allocator itself, not
 * a wrapper around one. This header is that allocator, written once
 * against src/internal/plat_pages.h's minimal "give me N zeroed
 * bytes, page-granular, no questions asked" contract rather than
 * against any one platform's raw syscalls, specifically so it does
 * not have to be rewritten per platform: including this header and
 * supplying __plat_pages_alloc()/__plat_pages_free() is the entire
 * cost of a new PLATFORM getting a working malloc() this way -- see
 * src/malloc/linux/plat_malloc.c for the (small) Linux half of that
 * split.
 *
 * Included as a header rather than built as its own PLAT_GLOBS object
 * for the same reason src/math/ldbl_math.h and src/math/aarch64_math.h are:
 * NT must never link this allocator at all (RtlAllocateHeap is
 * strictly the better choice there, see above), so this cannot be a
 * base file every platform compiles by default with NT overriding it
 * -- the override machinery is filename-keyed, not symbol-keyed, and
 * NT's own src/malloc/nt/plat_malloc.c already provides the very
 * __plat_alloc()/__plat_realloc()/__plat_alloc_size()/__plat_dealloc()
 * names this header defines. A platform opts into this allocator by
 * #include-ing it from its own plat_malloc.c, the same way a math
 * backend opts into ldbl_math.h's non-x86 branch; nothing opts in by
 * default.
 *
 * Deliberately NOT a general-purpose, competitively-tuned allocator
 * (no coalescing of adjacent free chunks, no arena reclamation, no
 * thread-local caches): a segregated free-list design, one free list
 * per power-of-two size class, backed by page-source-provided slabs
 * that are carved up and never returned to the platform. What this
 * buys, and why it is the right tradeoff for a from-scratch allocator
 * specifically: no boundary-tag bookkeeping and no merge logic at
 * all, which is where a hand-rolled allocator most often gets subtly
 * wrong in exactly the way that is hardest to catch (silent heap
 * corruption that only crashes some unrelated allocation much later)
 * -- see fuzz/linux_pilot_test_malloc.c for how that risk was
 * actually checked, not just reasoned about. The cost is real but
 * bounded: a size class never shrinks once grown, so a program with
 * one large allocation burst and a long quiet tail keeps that memory
 * mapped for its own lifetime. Acceptable for a conformance-suite-
 * shaped workload; a real tuned allocator is separate, future work if
 * some platform ever needs one.
 *
 * Layout: every live allocation is preceded by a 16-byte header
 * (struct chunk_hdr) holding its own usable size and which free list
 * it belongs to (or -1 for a "large" allocation, one page-source
 * mapping per allocation, no size class involved). 16 bytes keeps the
 * header itself, and therefore the user pointer right after it, a
 * multiple of 16 as long as every class size and the slab size are
 * too -- the same alignment src/malloc/nt/plat_malloc.c's own banner
 * documents NT's heap already gives on x86_64.
 *
 * Thread safety: a dedicated spinlock private to this header (below)
 * guards every free-list read/write -- NOT __plat_fast_lock()/
 * __plat_fast_unlock() (src/internal/plat_thread.h), despite an earlier
 * version of this file using exactly that lock, because it is the
 * SAME process-wide lock src/thread/pthread_atfork.c's pthread_atfork()
 * (growing its handler table with realloc()) and src/thread/semaphore.c's
 * sem_close()/sem_unlink() (freeing a record's path with free()) already
 * hold across a call into malloc()/free() -- and __plat_fast_lock() is
 * deliberately, explicitly non-recursive (src/thread/linux/plat_thread.c's
 * own banner: "Recursive acquisition by the same thread deadlocks here
 * exactly like RtlAcquirePebLock() would on NT"). Sharing one lock
 * between the pthread subsystem's own tables and this allocator's free
 * lists therefore made every such call site self-deadlock the instant
 * its critical section needed to allocate or free -- confirmed with
 * strace against a real fork/1-1.c run: the process spins forever in
 * sched_yield() (fast_lock's own spin body) immediately after a
 * sem_unlink()-triggered free(), never reaching the futex_wake() syscall
 * its very next statement, namespace_unlock(), would otherwise issue.
 * NT never hits this: src/malloc/nt/plat_malloc.c forwards straight to
 * RtlAllocateHeap()/RtlFreeHeap(), which take the OS's own private heap
 * lock, entirely independent of RtlAcquirePebLock()/RtlReleasePebLock()
 * -- so pthread_atfork()'s realloc() while PEB-locked has always been
 * safe there. This header's own separate lock restores that same
 * independence on Linux instead of teaching every current and future
 * fast_lock-held call site to avoid allocating, which is the more
 * fragile fix (one missed call site away from the same deadlock) for
 * a decision that belongs entirely to this allocator. No thread is
 * actually spawned by most programs that include this header (real
 * concurrent Linux pthreads have no backend yet -- a separate,
 * disclosed gap), but src/thread/aio.c's own POSIX AIO worker already
 * uses real CLONE_VM-sharing threads (src/thread/linux/plat_thread.c's
 * __plat_thread_spawn()) and would corrupt these lists instantly
 * without a real lock; the spin cost is unobservable either way. */
#ifndef _NTLIBC_PLAT_MALLOC_GENERIC_H
#define _NTLIBC_PLAT_MALLOC_GENERIC_H

#include "plat_malloc.h"
#include "plat_pages.h"
#include "plat_thread.h"

/* __builtin_memset/__builtin_memcpy, not <string.h>'s memset()/
 * memcpy(): this header sits directly below malloc()/calloc()/
 * realloc() in the call graph, and reaching for this project's own
 * memcpy.c/memset.c definitions here is exactly the kind of "which
 * came first" dependency this allocator has no reason to create when
 * the compiler builtins do the same job without it, real functions or
 * not. */

#define NTLIBC_MALLOC_PAGE_SIZE 4096u
#define NTLIBC_MALLOC_SLAB_BYTES ((size_t)64 * 1024u)
#define NTLIBC_MALLOC_HDR_SIZE 16u
#define NTLIBC_MALLOC_NUM_CLASSES 12 /* 16, 32, 64, ..., 16 << 11 = 32768 */

struct ntlibc_malloc_chunk_hdr {
	size_t size;  /* usable bytes available to the caller */
	long class;   /* index into free_list[], or -1 for a large (direct
	              * page-source mapping, one per allocation) chunk */
};

static void *ntlibc_malloc_free_list[NTLIBC_MALLOC_NUM_CLASSES];

/* This allocator's own private lock -- see this header's own banner for
 * why it is not __plat_fast_lock()/__plat_fast_unlock(). Same shape as
 * that lock (plain spin-CAS, yielding the CPU between attempts) and the
 * same reasoning applies to why a spin is the right tool here: every
 * critical section below is a handful of free-list pointer reads/writes,
 * never a blocking call (ntlibc_malloc_refill_locked() releases this
 * lock before its own __plat_pages_alloc() call, precisely so a real
 * mmap(2) round trip is never made under it), so the holder is always
 * running and always about to release. __plat_thread_alertable_yield()
 * (src/internal/plat_thread.h, a raw sched_yield(2) on this backend) is
 * reused rather than hand-rolling a second yield syscall wrapper in this
 * already-portable header. */
static int ntlibc_malloc_lock_word;

static void ntlibc_malloc_lock(void)
{
	int c;
	for (;;) {
		c = 0;
		if (__atomic_compare_exchange_n(&ntlibc_malloc_lock_word, &c, 1, 1,
		                                __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
			return;
		__plat_thread_alertable_yield();
	}
}

static void ntlibc_malloc_unlock(void)
{
	__atomic_store_n(&ntlibc_malloc_lock_word, 0, __ATOMIC_RELEASE);
}

static size_t ntlibc_malloc_class_size(int class) { return (size_t)NTLIBC_MALLOC_HDR_SIZE << class; }

static int ntlibc_malloc_class_for(size_t n)
{
	int class;
	size_t sz = NTLIBC_MALLOC_HDR_SIZE;
	for (class = 0; class < NTLIBC_MALLOC_NUM_CLASSES; class++, sz <<= 1)
		if (sz >= n) return class;
	return -1; /* too big for any class -- large path */
}

static size_t ntlibc_malloc_roundup_page(size_t n)
{
	return (n + (NTLIBC_MALLOC_PAGE_SIZE - 1)) & ~(size_t)(NTLIBC_MALLOC_PAGE_SIZE - 1);
}

/* Refill free_list[class] with one freshly page-sourced slab's worth
 * of chunks. Called with the lock held (matching every other
 * free_list access), released around __plat_pages_alloc() itself: a
 * page-fault-triggering kernel round trip is not the kind of thing
 * this process-wide lock should be held across, and nothing here
 * needs it to be -- worst case two threads both refill the same class
 * at once and the second one's extra chunks simply also go on the
 * list. */
static void ntlibc_malloc_refill_locked(int class)
{
	size_t csz = ntlibc_malloc_class_size(class);
	size_t stride = NTLIBC_MALLOC_HDR_SIZE + csz;
	size_t n = NTLIBC_MALLOC_SLAB_BYTES / stride;
	unsigned char *slab;
	size_t i;

	ntlibc_malloc_unlock();
	slab = __plat_pages_alloc(NTLIBC_MALLOC_SLAB_BYTES);
	ntlibc_malloc_lock();
	if (!slab) return;

	for (i = 0; i < n; i++) {
		struct ntlibc_malloc_chunk_hdr *h = (struct ntlibc_malloc_chunk_hdr *)(slab + i * stride);
		void *user = (unsigned char *)h + NTLIBC_MALLOC_HDR_SIZE;
		h->size = csz;
		h->class = class;
		*(void **)user = ntlibc_malloc_free_list[class];
		ntlibc_malloc_free_list[class] = user;
	}
}

void *__plat_alloc(size_t n, int zero)
{
	struct ntlibc_malloc_chunk_hdr *h;
	void *user;
	int class;

	if (n == 0) n = 1;

	if (n > ntlibc_malloc_class_size(NTLIBC_MALLOC_NUM_CLASSES - 1)) {
		/* Large path: one mapping, no free list involved. */
		size_t total = ntlibc_malloc_roundup_page(NTLIBC_MALLOC_HDR_SIZE + n);
		unsigned char *base = __plat_pages_alloc(total);
		if (!base) return 0;
		h = (struct ntlibc_malloc_chunk_hdr *)base;
		h->size = n;
		h->class = -1;
		user = base + NTLIBC_MALLOC_HDR_SIZE;
		/* __plat_pages_alloc() already returns zeroed memory -- memset
		 * anyway rather than trust that invariant silently at every
		 * call site; see this header's own banner on correctness over
		 * micro-optimisation. */
		if (zero) __builtin_memset(user, 0, n);
		return user;
	}

	class = ntlibc_malloc_class_for(n);
	ntlibc_malloc_lock();
	if (!ntlibc_malloc_free_list[class]) ntlibc_malloc_refill_locked(class);
	if (!ntlibc_malloc_free_list[class]) { ntlibc_malloc_unlock(); return 0; }
	user = ntlibc_malloc_free_list[class];
	ntlibc_malloc_free_list[class] = *(void **)user;
	ntlibc_malloc_unlock();

	h = (struct ntlibc_malloc_chunk_hdr *)((unsigned char *)user - NTLIBC_MALLOC_HDR_SIZE);
	if (zero) __builtin_memset(user, 0, h->size);
	return user;
}

size_t __plat_alloc_size(void *p)
{
	struct ntlibc_malloc_chunk_hdr *h = (struct ntlibc_malloc_chunk_hdr *)((unsigned char *)p - NTLIBC_MALLOC_HDR_SIZE);
	return h->size;
}

void __plat_dealloc(void *p)
{
	struct ntlibc_malloc_chunk_hdr *h;
	if (!p) return;
	h = (struct ntlibc_malloc_chunk_hdr *)((unsigned char *)p - NTLIBC_MALLOC_HDR_SIZE);
	if (h->class < 0) {
		__plat_pages_free(h, ntlibc_malloc_roundup_page(NTLIBC_MALLOC_HDR_SIZE + h->size));
		return;
	}
	ntlibc_malloc_lock();
	*(void **)p = ntlibc_malloc_free_list[h->class];
	ntlibc_malloc_free_list[h->class] = p;
	ntlibc_malloc_unlock();
}

/* No in-place growth/shrink attempted (a free-list-of-classes design
 * has no adjacent-chunk bookkeeping to check for room to extend into,
 * on purpose -- see this header's own banner): always a fresh
 * allocation, a copy of the smaller of the two sizes, and a free of
 * the original. Simple, and the copy is bounded by real, exact sizes
 * both ends already track precisely, so there is no scope for an
 * over-read here even though it is not the fastest possible realloc. */
void *__plat_realloc(void *p, size_t n)
{
	struct ntlibc_malloc_chunk_hdr *h;
	void *q;
	size_t old;

	if (!p) return __plat_alloc(n, 0);
	h = (struct ntlibc_malloc_chunk_hdr *)((unsigned char *)p - NTLIBC_MALLOC_HDR_SIZE);
	old = h->size;
	if (n == 0) { __plat_dealloc(p); return 0; }
	q = __plat_alloc(n, 0);
	if (!q) return 0;
	__builtin_memcpy(q, p, old < n ? old : n);
	__plat_dealloc(p);
	return q;
}

#endif
