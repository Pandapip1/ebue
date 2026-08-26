/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The four AddressSanitizer entry points fuzz/ntstubs.c calls, supplied
 * for the build that has no AddressSanitizer in it.
 *
 * ntstubs.c answers RtlAllocateHeap/RtlFreeHeap/RtlReAllocateHeap/
 * RtlSizeHeap with __interceptor_malloc and friends, and it does so on
 * purpose: routing ntlibc's heap through ASan's allocator is what makes
 * every ntlibc allocation visible to ASan and to LeakSanitizer with a
 * full ntlibc stack behind it (see the LeakSanitizer paragraph in
 * tools/asan-build.sh).  In NTLIBC_SAN_MODE=ubsan there is no such
 * allocator, and those four names are simply undefined.
 *
 * WHAT THIS FILE IS AND IS NOT.  It is a name-resolution shim, not a
 * replacement allocator: each function forwards to the host libc's, so
 * the behaviour ntstubs.c gets is glibc's malloc rather than ASan's.
 * That is the whole of the difference, and it is exactly the capability
 * that mode loses -- no redzones, so no heap-buffer-overflow detection;
 * no quarantine, so no use-after-free or double-free detection; no
 * LeakSanitizer.  Nothing here can put those back, and nothing here
 * should pretend to.
 *
 * WHY dlsym RATHER THAN CALLING malloc().  For the same reason
 * fuzz/host_oracle.c does it: `malloc` in this link is *ntlibc's* --
 * src/malloc/malloc.o is in the link and the static linker binds to the
 * definition it can already see.  Calling it here would route
 * RtlAllocateHeap back into the allocator that is implemented in terms
 * of RtlAllocateHeap.  Going through an explicit libc.so.6 handle
 * reaches glibc's, and cannot see ntlibc's hidden-visibility ones at
 * all.
 *
 * Compiled against the HOST headers, like host_oracle.c and unlike
 * everything else in this directory -- it has to agree with glibc about
 * size_t and about malloc_usable_size, not with ntlibc.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>

static void *libc(void)
{
	static void *h;
	if (!h) {
		h = dlopen("libc.so.6", RTLD_LAZY | RTLD_LOCAL);
		if (!h) { fprintf(stderr, "ubsanshim: %s\n", dlerror()); abort(); }
	}
	return h;
}

static void *sym(const char *n)
{
	void *p = dlsym(libc(), n);
	if (!p) { fprintf(stderr, "ubsanshim: no %s\n", n); abort(); }
	return p;
}

void *__interceptor_malloc(size_t n)
{
	static void *(*f)(size_t);
	if (!f) f = (void *(*)(size_t))sym("malloc");
	return f(n);
}

void __interceptor_free(void *p)
{
	static void (*f)(void *);
	if (!f) f = (void (*)(void *))sym("free");
	f(p);
}

void *__interceptor_realloc(void *p, size_t n)
{
	static void *(*f)(void *, size_t);
	if (!f) f = (void *(*)(void *, size_t))sym("realloc");
	return f(p, n);
}

/* RtlSizeHeap's answer.  malloc_usable_size() is glibc's nearest
 * equivalent to __sanitizer_get_allocated_size() and differs in one way
 * that matters to a reader of this code: it returns the size of the
 * BUCKET, which is >= the requested size, where the sanitizer returns
 * exactly what was asked for.  Anything in ntlibc that trusted
 * RtlSizeHeap as an exact figure would therefore be handed a larger
 * number here than under ASan.  Its one caller today is ntlibc's own
 * malloc_usable_size() (src/malloc/malloc.c), whose documented answer
 * IS the usable bucket size rather than the requested one -- so for that
 * caller this is not merely safe, it is the more faithful of the two.
 * Recorded anyway, because "right for today's only caller" is not the
 * same claim as "equivalent", and the next caller may want the other
 * one. */
size_t __sanitizer_get_allocated_size(const void *p)
{
	static size_t (*f)(void *);
	if (!f) f = (size_t (*)(void *))sym("malloc_usable_size");
	return f((void *)p);
}

/* ------------------------------------------- LeakSanitizer's two switches
 *
 * __lsan_disable()/__lsan_enable() bracket a region whose allocations
 * LeakSanitizer should not report.  fuzz/fuzz_shparse.c uses them around
 * a deliberately-leaked parse, and libFuzzer's own ExternalFunctions
 * constructor references them too, so without definitions the link fails
 * outright in this mode -- which is how this gap was found, by building
 * every harness rather than the ones this work touched.
 *
 * No-ops, and exactly right as no-ops: the request is "suspend leak
 * detection here", and in a build with no LeakSanitizer there is no leak
 * detection to suspend.  This is NOT the shim quietly satisfying a check
 * that should have failed -- the check is absent for the whole run, not
 * only inside the bracket, and that absence is stated everywhere this
 * mode is described.  A harness whose point is a leak assertion is
 * vacuous here, and fuzz_shparse's is: its leak fence asserts nothing in
 * this mode.  The mode-wide statement covers it; a per-call warning
 * would fire millions of times and be read by nobody.
 */
void __lsan_disable(void) { }
void __lsan_enable(void) { }
