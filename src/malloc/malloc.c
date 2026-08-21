/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * malloc on top of the NT heap.
 *
 * Every process has a heap ntdll made for it before the first instruction
 * of the program ran, and ntdll's own allocator is a serious one: size
 * classes, a low-fragmentation front end, coalescing, guard pages under
 * the debugger.  Writing another on top of NtAllocateVirtualMemory would
 * be more code for a worse result, so malloc is RtlAllocateHeap on the
 * process heap.  The heap lives in the address space, so it survives
 * fork the same way every other piece of memory does.
 *
 * RtlAllocateHeap(0) returns a usable pointer, which is what malloc(0)
 * is allowed to do.  Alignment is 8 on i386 and 16 on x86_64, which is
 * what the heap gives; aligned_alloc for anything larger over-allocates
 * and remembers the real block just before the returned one.
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

void *malloc(size_t n)
{
	void *p = RtlAllocateHeap(__process_heap(), 0, n);
	if (!p) errno = ENOMEM;
	return p;
}

void *calloc(size_t m, size_t n)
{
	void *p;
	if (n && m > (size_t)-1 / n) { errno = ENOMEM; return 0; }
	p = RtlAllocateHeap(__process_heap(), HEAP_ZERO_MEMORY, m * n);
	if (!p) errno = ENOMEM;
	return p;
}

void *realloc(void *p, size_t n)
{
	void *q;
	if (!p) return malloc(n);
	q = RtlReAllocateHeap(__process_heap(), 0, p, n);
	if (!q) errno = ENOMEM;
	return q;
}

void *__malloc(size_t n) { return malloc(n); }
void __free(void *p) { free(p); }

size_t malloc_usable_size(void *p)
{
	return p ? RtlSizeHeap(__process_heap(), 0, p) : 0;
}

void *reallocarray(void *p, size_t m, size_t n)
{
	if (n && m > (size_t)-1 / n) { errno = ENOMEM; return 0; }
	return realloc(p, m * n);
}

/* Blocks with alignment above the heap's own are carved out of a larger
 * heap block, and the pair (returned pointer, real block) is remembered
 * in a small list so that free can hand the real block back.  Aligned
 * allocation is rare enough that a list is the right structure. */
struct aligned_rec { void *user, *base; struct aligned_rec *next; };
static struct aligned_rec *aligned_list;

int posix_memalign(void **res, size_t align, size_t len)
{
	void *base, *p;
	struct aligned_rec *r;
	if (align < sizeof(void *) || (align & (align - 1))) return EINVAL;
	if (align <= 2 * sizeof(void *)) {
		p = malloc(len);
		if (!p) return ENOMEM;
		*res = p;
		return 0;
	}
	r = malloc(sizeof *r);
	if (!r) return ENOMEM;
	base = malloc(len + align);
	if (!base) { free(r); return ENOMEM; }
	p = (void *)(((uintptr_t)base + align - 1) & ~(uintptr_t)(align - 1));
	r->user = p; r->base = base; r->next = aligned_list; aligned_list = r;
	*res = p;
	return 0;
}

void *aligned_alloc(size_t align, size_t len)
{
	void *p;
	int e = posix_memalign(&p, align < sizeof(void *) ? sizeof(void *) : align, len);
	if (e) { errno = e; return 0; }
	return p;
}

void *memalign(size_t align, size_t len) { return aligned_alloc(align, len); }
void *valloc(size_t len) { return aligned_alloc(4096, len); }

void free(void *p)
{
	struct aligned_rec **pp;
	if (!p) return;
	for (pp = &aligned_list; *pp; pp = &(*pp)->next) {
		if ((*pp)->user == p) {
			struct aligned_rec *r = *pp;
			*pp = r->next;
			RtlFreeHeap(__process_heap(), 0, r->base);
			RtlFreeHeap(__process_heap(), 0, r);
			return;
		}
	}
	RtlFreeHeap(__process_heap(), 0, p);
}
