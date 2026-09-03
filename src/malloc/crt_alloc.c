/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __malloc()/__free(): the allocator crt/crt1.c uses to build argv/envp
 * before main() runs. They use their own token domain,
 * internal_heap_allocated, separate from malloc()/free()'s
 * heap_allocated, and skip POSIX-facing behavior (errno on failure,
 * posix_memalign()'s aligned_rec bookkeeping) that startup code has no
 * use for.
 *
 * This is its own translation unit, not two wrapper functions in
 * malloc.c, because crt1.o is first in every link and references
 * __malloc/__free unconditionally; a static archive pulls in a needed
 * symbol's whole .o, so sharing an object with the public
 * malloc()/calloc()/free() used to drag those into every program,
 * including ones that interpose their own (a real duplicate-symbol
 * link error against musl's flockfile-list.c regression test).
 */
#include "libc.h"
#include "plat_malloc.h"

withtok(internal_heap_allocated)
withtok(writable_span(n))
void *__malloc(size_t n)
{
	return __plat_alloc(n, 0);
}

void __free(void *p consume(internal_heap_allocated))
{
	if (!p) return;
	__plat_dealloc(p);
}
