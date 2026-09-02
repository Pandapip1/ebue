/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __malloc()/__free(): the allocator crt/crt1.c uses to build argv/
 * envp before main() runs (see its own calls: the argv vector, the
 * oversized-argv reallocation, and the envp vector).  These carry
 * their own memory-token domain, internal_heap_allocated, kept
 * deliberately separate from malloc()/free()'s heap_allocated
 * (src/internal/libc.h, src/malloc/malloc.c) -- CRT startup code is
 * not a POSIX caller and this pair skips POSIX-facing behaviour
 * (malloc()'s errno on failure and the *pointer identity* free()
 * checks against posix_memalign()'s aligned_rec list, which nothing
 * allocated here can ever be in) that public code depends on and
 * startup code has no use for.
 *
 * MECHANICAL REASON THIS IS ITS OWN TRANSLATION UNIT, not just two
 * more functions in src/malloc/malloc.c (where they used to live,
 * as thin `return malloc(n)` / `free(p)` wrappers): crt1.o is the
 * first object in every single link this project produces (every
 * Makefile rule that builds an executable, and every external test
 * harness invocation -- tools/libc-test.sh's build_one() among them
 * -- passes lib/crt1.o ahead of the program itself), and it references
 * __malloc/__free unconditionally.  A static archive resolves an
 * unresolved reference at OBJECT granularity: whichever .o inside
 * lib/libc.a happens to define the needed symbol is pulled in whole,
 * every other symbol that .o also defines along for the ride.  When
 * __malloc/__free lived in the same .o as the public malloc()/
 * calloc()/free()/realloc()/aligned_alloc(), crt1.o's need for the
 * first two was enough to pull the whole object into EVERY program --
 * including one that interposes its own malloc()/calloc()/free() (the
 * documented, POSIX-legal thing musl's own flockfile-list.c regression
 * test does; test/libc-test-expected.txt's `flockfile-list` row
 * recorded the resulting "link symbol 'malloc' defined twice").  The
 * fix is not about behaviour, and not about which token domain
 * __malloc/__free use -- it is that a symbol crt1.o needs
 * unconditionally must not share a translation unit with symbols only
 * a program that has NOT defined its own are entitled to expect the
 * archive to provide.
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
