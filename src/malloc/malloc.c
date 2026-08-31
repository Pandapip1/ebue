/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The POSIX-facing allocator front door: argument validation and the
 * overflow checks (calloc()/reallocarray()) stay here, portable across
 * every platform; the four primitives with no portable equivalent --
 * allocate, resize, query a live allocation's usable size, free --
 * live behind src/internal/plat_malloc.h (see that header and each
 * platform's own plat_malloc.c for why: NT delegates to ntdll's own
 * process heap, already a serious allocator with size classes,
 * coalescing and guard-page support; a platform with nothing
 * equivalent to delegate to, like Linux, needs a real one written out
 * -- see src/malloc/linux/plat_malloc.c's own banner).
 *
 * posix_memalign()/aligned_alloc()/memalign()/valloc() below were
 * already expressed purely in terms of malloc()/free() rather than any
 * raw heap call, so they needed no change at all from the split that
 * produced this file's own shape.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "libc.h"
#include "plat_malloc.h"

void *malloc(size_t n)
{
	void *p = __plat_alloc(n, 0);
	if (!p) errno = ENOMEM;
	return p;
}

void *calloc(size_t m, size_t n)
{
	void *p;
	if (n && m > (size_t)-1 / n) { errno = ENOMEM; return 0; }
	p = __plat_alloc(m * n, 1);
	if (!p) errno = ENOMEM;
	return p;
}

void *realloc(void *p, size_t n)
{
	void *q;
	if (!p) return malloc(n);
	q = __plat_realloc(p, n);
	if (!q) errno = ENOMEM;
	return q;
}

void *__malloc(size_t n) { return malloc(n); }
void __free(void *p) { free(p); }

size_t malloc_usable_size(void *p)
{
	return p ? __plat_alloc_size(p) : 0;
}

void *reallocarray(void *p, size_t m, size_t n)
{
	if (n && m > (size_t)-1 / n) { errno = ENOMEM; return 0; }
	return realloc(p, m * n); // NOLINT(clang-analyzer-optin.portability.UnixAPI) -- realloc(p, 0) is a deliberate, defined passthrough here
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
	if (len > (size_t)-1 - align) return ENOMEM;
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
			__plat_dealloc(r->base);
			__plat_dealloc(r);
			return;
		}
	}
	__plat_dealloc(p);
}

// NOLINTEND(misc-include-cleaner)
