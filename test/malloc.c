/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* reallocarray() is feature-test gated in include/stdlib.h; same define
 * most other test/*.c already carry for the same reason (see
 * test/posix-glob.c's comment on this exact define). */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

/* This test asserts that malloc() returns NULL, with errno == ENOMEM, for a
 * request that cannot possibly be satisfied -- exactly what C99 7.20.3.3p3
 * requires of malloc().  AddressSanitizer's default
 * allocator_may_return_null=0 makes ASan's own allocator abort the process
 * on such a request, so the behaviour under test is never reached.  Setting
 * the option weakens nothing: it tells ASan to act like a conforming
 * allocator and hand back NULL, which is the thing being checked.  There is
 * no per-call-site control -- the abort is inside ASan's malloc interceptor,
 * not in instrumented code -- so a flag is the narrowest tool there is, and
 * this hook keeps it to this one test.
 *
 * ASan reads this weak hook at start-up, so the option travels with the
 * binary.  It is ignored with the *dynamic* runtime (-shared-libasan, which
 * tools/asan-build.sh needs for unrelated reasons): libclang_rt.asan.so
 * carries its own weak definition and does not let ours preempt it.  That
 * script therefore also passes the option in the environment, for this test
 * alone.  The hook still earns its place in a static-runtime build, where it
 * is what makes the test self-contained.  Inert in the tcc/Wine build.  */
#if defined(__SANITIZE_ADDRESS__) || \
    (defined(__has_feature) && __has_feature(address_sanitizer))
const char *__asan_default_options(void);
const char *__asan_default_options(void) { return "allocator_may_return_null=1"; }
#endif

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static int aligned_to(const void *p, size_t a)
{
	return ((uintptr_t)p & (a - 1)) == 0;
}

static void fill(unsigned char *p, size_t n, unsigned seed)
{
	size_t i;
	for (i = 0; i < n; i++) p[i] = (unsigned char)(seed + i * 7);
}

static int verify(const unsigned char *p, size_t n, unsigned seed)
{
	size_t i;
	for (i = 0; i < n; i++) if (p[i] != (unsigned char)(seed + i * 7)) return 0;
	return 1;
}

static unsigned rng_state = 0xdeadbeef;
static unsigned rnd(void)
{
	rng_state = rng_state * 1103515245 + 12345;
	return rng_state >> 16;
}

int main(void)
{
	int i;

	/* malloc/free basic, with pattern */
	{
		unsigned char *p = malloc(100);
		CHECK(p != 0);
		if (p) { fill(p, 100, 1); CHECK(verify(p, 100, 1)); }
		free(p);
		free(0);
	}
	/* malloc(0): source says RtlAllocateHeap(0) gives a usable pointer */
	{
		void *p = malloc(0);
		CHECK(p != 0);
		free(p);
	}
	/* calloc zero-fills, overflow */
	{
		unsigned char *p = calloc(300, 3);
		CHECK(p != 0);
		if (p) { for (i = 0; i < 900; i++) CHECK(p[i] == 0); }
		free(p);
		errno = 0;
		p = calloc(SIZE_MAX / 2, 4);
		CHECK(p == 0);
		CHECK(errno == ENOMEM);
		errno = 0;
		p = calloc(SIZE_MAX, SIZE_MAX);
		CHECK(p == 0 && errno == ENOMEM);
		p = calloc(0, 0);
		free(p);
		p = calloc(0, 5);
		free(p);
	}
	/* realloc grow/shrink preserves contents */
	{
		unsigned char *p = malloc(64), *q;
		CHECK(p != 0);
		fill(p, 64, 9);
		q = realloc(p, 4096);
		CHECK(q != 0);
		CHECK(verify(q, 64, 9));
		fill(q, 4096, 11);
		q = realloc(q, 16);
		CHECK(q != 0);
		CHECK(verify(q, 16, 11));
		q = realloc(q, 1 << 20);
		CHECK(q != 0);
		CHECK(verify(q, 16, 11));
		free(q);
	}
	/* realloc(NULL, n) == malloc(n) */
	{
		unsigned char *p = realloc(0, 50);
		CHECK(p != 0);
		if (p) { fill(p, 50, 3); CHECK(verify(p, 50, 3)); }
		free(p);
	}
	/* realloc(p, 0): RtlReAllocateHeap(0) gives a (non-NULL) block; whichever
	 * way it goes, it must not crash and the result must be free-able. */
	{
		void *p = malloc(10), *q;
		CHECK(p != 0);
		q = realloc(p, 0);
		if (q) free(q);
		else CHECK(errno == ENOMEM); /* p was not freed; leak is fine here */
	}
	/* reallocarray */
	{
		void *p;
		errno = 0;
		p = reallocarray(0, SIZE_MAX / 2, 4);
		CHECK(p == 0 && errno == ENOMEM);
		p = reallocarray(0, 10, 10);
		CHECK(p != 0);
		p = reallocarray(p, 100, 10);
		CHECK(p != 0);
		free(p);
	}
	/* malloc_usable_size */
	{
		void *p = malloc(100);
		CHECK(p != 0);
		CHECK(malloc_usable_size(p) >= 100);
		CHECK(malloc_usable_size(0) == 0);
		free(p);
	}
	/* posix_memalign: errors */
	{
		void *p = (void *)1;
		CHECK(posix_memalign(&p, 3, 10) == EINVAL);
		CHECK(posix_memalign(&p, 24, 10) == EINVAL);
		CHECK(posix_memalign(&p, sizeof(void *) / 2, 10) == EINVAL);
		CHECK(posix_memalign(&p, 0, 10) == EINVAL);
		CHECK(p == (void *)1);
	}
	/* posix_memalign: alignment honoured, memory usable */
	{
		static const size_t aligns[] = { 8, 16, 32, 64, 256, 4096, 65536 };
		size_t k;
		for (k = 0; k < sizeof aligns / sizeof aligns[0]; k++) {
			void *p = 0;
			size_t a = aligns[k];
			int r = posix_memalign(&p, a, 1000);
			CHECK(r == 0);
			CHECK(p != 0);
			CHECK(aligned_to(p, a));
			if (p) {
				fill(p, 1000, (unsigned)a);
				CHECK(verify(p, 1000, (unsigned)a));
			}
			free(p);
		}
		if (sizeof(void *) >= 8) {
			void *p = 0;
			CHECK(posix_memalign(&p, 8, 10) == 0 && p != 0);
			free(p);
		}
		{
			void *p = 0;
			CHECK(posix_memalign(&p, 4096, 0) == 0);
			free(p);
		}
	}
	/* aligned_alloc / memalign / valloc */
	{
		void *p;
		size_t huge = SIZE_MAX & ~(size_t)4095;
		errno = 0;
		p = aligned_alloc(48, 100);
		CHECK(p == 0 && errno == EINVAL);
		errno = 0;
		p = memalign(100, 100);
		CHECK(p == 0 && errno == EINVAL);
		p = aligned_alloc(1024, 2048);
		CHECK(p != 0 && aligned_to(p, 1024));
		if (p) { fill(p, 2048, 5); CHECK(verify(p, 2048, 5)); }
		free(p);
		p = aligned_alloc(1, 16); /* below sizeof(void*) is bumped up */
		CHECK(p != 0);
		free(p);
		p = memalign(128, 1);
		CHECK(p != 0 && aligned_to(p, 128));
		free(p);
		p = valloc(100);
		CHECK(p != 0 && aligned_to(p, 4096));
		if (p) memset(p, 0xaa, 100);
		free(p);

		/* The over-allocation used to align a block must itself be
		 * checked.  `huge` is a valid multiple of 4096 (including for
		 * aligned_alloc's size restriction), but huge+4096 wraps. */
		p = (void *)1;
		CHECK(posix_memalign(&p, 4096, huge) == ENOMEM);
		CHECK(p == (void *)1);
		errno = 0;
		CHECK(aligned_alloc(4096, huge) == 0 && errno == ENOMEM);
		errno = 0;
		CHECK(memalign(4096, huge) == 0 && errno == ENOMEM);
		errno = 0;
		CHECK(valloc(huge) == 0 && errno == ENOMEM);
	}
	/* aligned blocks: fill and check non-overlap, free in various orders */
	{
		enum { N = 64 };
		void *p[N];
		int order[N];
		int pass;
		for (pass = 0; pass < 3; pass++) {
			for (i = 0; i < N; i++) {
				size_t a = (size_t)64 << (i % 5); /* 64..1024 */
				p[i] = aligned_alloc(a, 200);
				CHECK(p[i] != 0 && aligned_to(p[i], a));
				if (p[i]) fill(p[i], 200, (unsigned)i);
			}
			for (i = 0; i < N; i++) CHECK(p[i] && verify(p[i], 200, (unsigned)i));
			for (i = 0; i < N; i++) order[i] = i;
			if (pass == 1) for (i = 0; i < N; i++) order[i] = N - 1 - i;
			if (pass == 2) for (i = N - 1; i > 0; i--) {
				int j = (int)(rnd() % (unsigned)(i + 1)), t = order[i];
				order[i] = order[j]; order[j] = t;
			}
			/* free half, verify the survivors, then free the rest */
			for (i = 0; i < N / 2; i++) { free(p[order[i]]); p[order[i]] = 0; }
			for (i = 0; i < N; i++) if (p[i]) CHECK(verify(p[i], 200, (unsigned)i));
			for (i = N / 2; i < N; i++) free(p[order[i]]);
		}
	}
	/* normal blocks interleaved with aligned ones; free() must route right */
	{
		void *n[16], *a[16];
		for (i = 0; i < 16; i++) {
			n[i] = malloc(100);
			a[i] = aligned_alloc(256, 100);
			CHECK(n[i] && a[i] && aligned_to(a[i], 256));
			if (n[i]) fill(n[i], 100, 100 + i);
			if (a[i]) fill(a[i], 100, 200 + i);
		}
		for (i = 0; i < 16; i++) {
			CHECK(verify(n[i], 100, 100 + i));
			CHECK(verify(a[i], 100, 200 + i));
		}
		for (i = 0; i < 16; i += 2) free(n[i]);
		for (i = 1; i < 16; i += 2) free(a[i]);
		for (i = 1; i < 16; i += 2) CHECK(verify(n[i], 100, 100 + i));
		for (i = 0; i < 16; i += 2) CHECK(verify(a[i], 100, 200 + i));
		for (i = 1; i < 16; i += 2) free(n[i]);
		for (i = 0; i < 16; i += 2) free(a[i]);
		/* a plain block allocated after aligned ones were freed */
		{
			void *p = malloc(8);
			CHECK(p != 0);
			free(p);
		}
	}
	/* many small allocs + frees */
	{
		enum { N = 10000 };
		static void *p[N];
		for (i = 0; i < N; i++) {
			size_t sz = rnd() % 128 + 1;
			p[i] = malloc(sz);
			CHECK(p[i] != 0);
			if (p[i]) memset(p[i], i & 0xff, sz);
		}
		for (i = 0; i < N; i += 2) { free(p[i]); p[i] = 0; }
		for (i = 1; i < N; i += 2) {
			p[i] = realloc(p[i], 256);
			CHECK(p[i] != 0);
			if (p[i]) CHECK(((unsigned char *)p[i])[0] == (i & 0xff));
		}
		for (i = 0; i < N; i++) free(p[i]);
	}
	/* large alloc: succeeds or fails cleanly with ENOMEM */
	{
		size_t big = (size_t)64 << 20;
		unsigned char *p;
		errno = 0;
		p = malloc(big);
		if (p) {
			p[0] = 1; p[big - 1] = 2;
			CHECK(p[0] == 1 && p[big - 1] == 2);
			free(p);
		} else {
			CHECK(errno == ENOMEM);
		}
		errno = 0;
		p = malloc(SIZE_MAX - 100);
		CHECK(p == 0 && errno == ENOMEM);
	}
	/* strdup/strndup return heap blocks that free/realloc accept */
	{
		char *s = strdup("hello, world");
		char *t = strndup("hello, world", 5);
		char *e = strdup("");
		CHECK(s && !strcmp(s, "hello, world"));
		CHECK(t && !strcmp(t, "hello") && strlen(t) == 5);
		CHECK(e && !*e);
		s = realloc(s, 100);
		CHECK(s && !strcmp(s, "hello, world"));
		if (s) { strcat(s, "!"); CHECK(!strcmp(s, "hello, world!")); }
		free(s); free(t); free(e);
		t = strndup("abc", 100);
		CHECK(t && !strcmp(t, "abc"));
		free(t);
	}

	if (!fails) printf("malloc: all tests passed\n");
	return fails != 0;
}
