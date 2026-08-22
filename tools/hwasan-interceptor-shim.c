/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * hwasan-interceptor-shim.c -- makes fuzz/ntstubs.c's allocator plumbing
 * link against libclang_rt.hwasan, without touching fuzz/ntstubs.c.
 *
 * ntstubs.c's vmalloc()/vfree() (the backing for RtlAllocateHeap and
 * friends -- see its own "the heap, on ASan's allocator" comment) call
 * __interceptor_malloc/__interceptor_free/__interceptor_realloc by name
 * on purpose, to reach the sanitizer's allocator directly.  ASan's shared
 * runtime exports that exact two-underscore symbol as a weak alias
 * (`nm -D libclang_rt.asan-*.so`), so tools/asan-build.sh needs nothing
 * extra.  libclang_rt.hwasan's runtime does not: it only exports the
 * mangled `___interceptor_malloc` (three underscores) plus a weak `malloc`
 * -- there is no two-underscore alias to bind to, so linking ntstubs.o
 * against it fails with "undefined reference to `__interceptor_malloc'"
 * and friends.
 *
 * This file supplies exactly those three symbols, forwarding to the plain
 * libc names.  That is not a workaround of missing instrumentation: with
 * -shared-libsan, HWASan already intercepts plain malloc/free/realloc
 * globally (they are the weak `malloc`/`free`/`realloc` in the runtime,
 * confirmed the same way above), so a call that reaches libc's malloc()
 * here is a call HWASan already tracks.  Built only into tools/hwasan-build.sh's
 * link; nothing in src/ or fuzz/ needs to know it exists.
 */
#include <stddef.h>
#include <stdlib.h>

void *__interceptor_malloc(size_t n)
{
	return malloc(n);
}

void __interceptor_free(void *p)
{
	free(p);
}

void *__interceptor_realloc(void *p, size_t n)
{
	return realloc(p, n);
}
