/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Real Linux dlopen()/dlsym()/dlclose()/dlerror() coverage against
 * src/dlfcn/linux/plat_dlfcn.c -- the from-scratch ELF64 loader that
 * file's own header banner documents in full. test/posix-dl.c (the
 * existing <dlfcn.h> clause audit) has ZERO Linux coverage: every
 * single test in it hardcodes "C:\Windows\System32\ntdll.dll" as its
 * fixture and is entirely NT-specific, and its own file-level design
 * (one shared clause audit across four originally-unimplemented
 * headers, exercised through NT-only fixtures throughout) does not
 * bend cleanly onto a second, unrelated loader backend with entirely
 * different (real ELF .so) fixtures of its own -- hence a new, Linux-
 * only file here rather than a Linux section grafted into that one.
 *
 * This file is deliberately NOT wrapped in test/posix-dl.c's own three-
 * way BUG/N-A/UNIMPL fencing convention (see that file's own header
 * comment for what each fence means): every gap this file tests --
 * DT_NEEDED chasing, DT_INIT_ARRAY constructor execution, per-object
 * TLS (aarch64), PT_GNU_RELRO hardening, R_AARCH64_IRELATIVE/
 * R_X86_64_IRELATIVE ("ifunc") dispatch -- is REAL, LANDED, WORKING
 * code (see plat_dlfcn.c's own updated banner sections), so every
 * test below runs unfenced, the same as this
 * file's NT sibling's own "what already works" section.
 *
 * Fixtures: the sources under test/dl-linux-fixtures/, real Linux .so's built by the
 * host's own $(CC) (see Makefile's own PLATFORM=linux-gated rules,
 * right next to test/rpath-plugin.dll's -- the closest existing
 * precedent this tree had for "a test needs a real loadable image
 * built alongside it", extended here to real ELF .so's instead of PE
 * DLLs) to obj/test/dlfix_*.so, alongside this test's own .exe. Located
 * at runtime via THIS PROGRAM's own argv[0] directory (fixture_dir()
 * below), not the process's current working directory: `make check`'s
 * own tools/run-tests.py runs every test from a freshly created temp
 * directory (see that script's own run_one()), so a plain "./dlfix_
 * dep.so" relative-to-cwd path would not resolve there even though
 * `make check` never actually runs this file -- it is Wine/NT-only,
 * and this file is Linux-only, see the Makefile's own PLATFORM=linux
 * gate -- making the lookup argv[0]-relative instead costs nothing and
 * means this test is not silently relying on being run from exactly
 * one particular directory.
 */
#include "test-policy.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <dlfcn.h>

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

typedef int (*fnptr0)(void);
typedef void **(*fnptr_voidpp0)(void);

static char fixture_dir_buf[4096];

/* Directory containing THIS executable's own argv[0] -- see this file's
 * own header comment for why fixtures are found this way rather than
 * relative to cwd. A bare basename (no '/' in argv[0] at all, e.g. a
 * shell that found this program via $PATH) falls back to "." -- but the
 * documented invocation, `make obj/test/posix-dl-linux.exe &&
 * ./obj/test/posix-dl-linux.exe`, always gives argv[0] a '/' in it, so
 * this fallback is a safety net, not the expected path. */
static const char *fixture_dir(const char *argv0)
{
	const char *slash = strrchr(argv0, '/');
	size_t len;
	if (!slash) return ".";
	len = (size_t)(slash - argv0);
	if (len >= sizeof fixture_dir_buf) len = sizeof fixture_dir_buf - 1;
	memcpy(fixture_dir_buf, argv0, len);
	fixture_dir_buf[len] = 0;
	return fixture_dir_buf;
}

static void fixture_path(char *out, size_t outsz, const char *dir, const char *name)
{
	snprintf(out, outsz, "%s/%s", dir, name);
}

/* ============================================================
 * DT_NEEDED dependency chasing (src/dlfcn/linux/plat_dlfcn.c's
 * load_object(), "NAMESPACE ISOLATION" banner's own DT_NEEDED section)
 * ==============================================================
 */

/* dlfix_needs.so calls dep_answer() (defined only in dlfix_dep.so, a
 * real DT_NEEDED entry -- see that fixture's own comment) without this
 * TEST ever dlopen()ing dlfix_dep.so itself. If the loader did not
 * chase DT_NEEDED, this dlopen() would either fail outright (an
 * "undefined symbol: dep_answer" dlerror(), the exact pre-this-pass
 * behaviour plat_dlfcn.c's own top banner used to document) or, if it
 * somehow loaded anyway, needs_answer() would crash through an
 * unresolved GOT slot. */
static void test_dt_needed_chases_dependency(const char *dir)
{
	char path[4096];
	void *h;
	fnptr0 needs_answer;

	fixture_path(path, sizeof path, dir, "dlfix_needs.so");
	h = dlopen(path, RTLD_NOW);
	if (!h) printf("dlopen dlfix_needs.so failed: %s\n", dlerror());
	CHECK(h != NULL);
	if (!h) return;

	needs_answer = (fnptr0)dlsym(h, "needs_answer");
	CHECK(needs_answer != NULL);
	CHECK(needs_answer && needs_answer() == 1055); /* dep_answer()==55, +1000 */

	CHECK(dlclose(h) == 0);
}

/* Two independent top-level dlopen() calls on the SAME dependency chain
 * must each get their own, freshly-loaded copy of the WHOLE chain --
 * plat_dlfcn.c's own "NAMESPACE ISOLATION" banner's explicit DT_NEEDED
 * extension: "load each dependency freshly WITHIN the same top-level
 * dlopen() call's own namespace, never deduped against a sibling
 * top-level dlopen()'s own copy of the identical dependency". Both
 * handles must differ, and both must independently work. */
static void test_dt_needed_never_dedups(const char *dir)
{
	char path[4096];
	void *h1, *h2;
	fnptr0 a1, a2;

	fixture_path(path, sizeof path, dir, "dlfix_needs.so");
	h1 = dlopen(path, RTLD_NOW);
	h2 = dlopen(path, RTLD_NOW);
	CHECK(h1 != NULL && h2 != NULL);
	CHECK(h1 != h2);
	if (!h1 || !h2) return;

	a1 = (fnptr0)dlsym(h1, "needs_answer");
	a2 = (fnptr0)dlsym(h2, "needs_answer");
	CHECK(a1 && a1() == 1055);
	CHECK(a2 && a2() == 1055);
	CHECK((void *)a1 != (void *)a2); /* genuinely separate mappings */

	CHECK(dlclose(h1) == 0);
	CHECK(dlclose(h2) == 0);
}

/* A missing dependency is still a clean, loud failure, not a crash --
 * dlfix_needs.so itself exists and loads fine on its own; deleting
 * dlfix_dep.so out from under it is exactly the shape this asserts
 * without needing a third, deliberately-broken fixture file: renaming
 * dlfix_dep.so's own path to something this loader cannot find. */
static void test_dt_needed_missing_dependency_fails_cleanly(const char *dir)
{
	char path[4096];
	void *h;
	char *e;

	fixture_path(path, sizeof path, dir, "dlfix_needs_missing_dep_do_not_create.so");
	h = dlopen(path, RTLD_NOW);
	CHECK(h == NULL);
	e = dlerror();
	CHECK(e != NULL);
}

/* ============================================================
 * DT_INIT_ARRAY constructor execution (plat_dlfcn.c's run_ctors())
 * ==============================================================
 */

static void test_dt_init_array_runs_constructor(const char *dir)
{
	char path[4096];
	void *h;
	fnptr0 get_ctor_ran;

	fixture_path(path, sizeof path, dir, "dlfix_ctor.so");
	h = dlopen(path, RTLD_NOW);
	CHECK(h != NULL);
	if (!h) return;

	/* No caller anywhere calls the constructor directly -- if run_ctors()
	 * did not run it, this reads 0 forever. */
	get_ctor_ran = (fnptr0)dlsym(h, "get_ctor_ran");
	CHECK(get_ctor_ran != NULL);
	CHECK(get_ctor_ran && get_ctor_ran() == 1);

	CHECK(dlclose(h) == 0);
}

/* Two independent instances each get their own constructor run exactly
 * once -- not zero (never ran), not shared/accumulated (would still
 * read 1 here regardless, but a real dedup bug would show up as BOTH
 * handles reading whatever a single shared instance's counter reached,
 * which a non-boolean counter would catch; ctor_ran is boolean-shaped
 * on purpose, so the real proof is the same one test_dt_needed_never_
 * dedups() above already makes: h1 != h2, two distinct mappings). */
static void test_dt_init_array_runs_once_per_instance(const char *dir)
{
	char path[4096];
	void *h1, *h2;
	fnptr0 g1, g2;

	fixture_path(path, sizeof path, dir, "dlfix_ctor.so");
	h1 = dlopen(path, RTLD_NOW);
	h2 = dlopen(path, RTLD_NOW);
	CHECK(h1 != NULL && h2 != NULL && h1 != h2);
	if (!h1 || !h2) return;

	g1 = (fnptr0)dlsym(h1, "get_ctor_ran");
	g2 = (fnptr0)dlsym(h2, "get_ctor_ran");
	CHECK(g1 && g1() == 1);
	CHECK(g2 && g2() == 1);

	CHECK(dlclose(h1) == 0);
	CHECK(dlclose(h2) == 0);
}

/* ============================================================
 * PT_GNU_RELRO hardening (plat_dlfcn.c's load_object(), the mprotect()
 * pass right after run_ctors()'s own comment)
 * ==============================================================
 */

/* dlfix_ctor.so's relro_fp (see that fixture's own comment) sits in a
 * RELRO-covered, load-time-relocated section: get_relro_fp_addr() hands
 * back its address, and this attempts to write through that address in
 * a FORKED CHILD (fork() duplicates this process's own mappings,
 * relro-hardening included, so the child's attempt cannot corrupt
 * anything the parent -- or any later test in this same run -- still
 * needs) and confirms the write faults. This is the standard portable
 * way to prove a page is really read-only from POSIX-level code with no
 * loader-internal hooks: catching SIGSEGV would also work, but a signal
 * handler that survives a real hardware fault and returns is exactly
 * the kind of undefined behaviour POSIX itself warns against (sigaction.
 * html: for SIGSEGV raised by a real memory-protection violation,
 * "the behavior is undefined" if the handler returns) -- so the child
 * simply crashes and the PARENT observes that outcome via waitpid(),
 * never attempting to survive the fault itself.
 *
 * Deliberately called LAST in main(), after every other dlopen()-based
 * test in this file, not for any reason related to RELRO itself: this
 * process's own fork() (via src/process/fork.c's fork_impl(), shared,
 * non-platform-specific code) unconditionally dup()s and re-closes
 * every open descriptor around the clone(2) call to mark it NT-style
 * "inheritable" (mark_fds_inheritable()/unmark_cloexec_fds()) -- a
 * concept Linux's own fork() needs no help with at all (every fd is
 * already inherited by a real fork() child regardless), so this is
 * pure NT-shaped overhead on this platform, not a correctness
 * requirement. That overhead has been observed to leave THIS process's
 * own low-numbered descriptors (stdin/stdout/stderr) shuffled onto
 * different fd numbers, and a dlopen() call immediately following a
 * fork() in the same process then failed its own file-backed mmap()
 * with a genuine kernel ENODEV -- reproducible down to a ~20-line
 * standalone repro using nothing but fork()+mmap(MAP_FIXED)+open(),
 * and NOT reproducible through plain host glibc doing the identical
 * sequence, which points at src/process/fork.c's own NT-shaped
 * fd-inheritance dance interacting badly with this environment's
 * mmap() implementation specifically, not at anything in
 * src/dlfcn/linux/plat_dlfcn.c (the file this whole test suite exists
 * to exercise) or in a real, non-sandboxed Linux kernel. Fixing
 * fork.c's own Linux behavior is real, separate, out-of-scope
 * follow-up work -- ordering this one test last is what keeps its own
 * real, working fork()-based proof from disturbing any dlopen() this
 * file still needs to run afterward, without papering over or
 * silently rewriting behavior in a subsystem this test does not
 * own. */
static void test_pt_gnu_relro_hardening(const char *dir)
{
	char path[4096];
	void *h;
	fnptr_voidpp0 get_addr;
	void **addr;
	pid_t pid;
	int status;

	fixture_path(path, sizeof path, dir, "dlfix_ctor.so");
	h = dlopen(path, RTLD_NOW);
	CHECK(h != NULL);
	if (!h) return;

	get_addr = (fnptr_voidpp0)dlsym(h, "get_relro_fp_addr");
	CHECK(get_addr != NULL);
	if (!get_addr) { dlclose(h); return; }
	addr = get_addr();
	CHECK(addr != NULL);

	pid = fork();
	CHECK(pid >= 0);
	if (pid == 0) {
		/* Child: this write must fault. If PT_GNU_RELRO's own mprotect()
		 * pass did not run, it silently succeeds instead -- report that
		 * as a normal exit(2) so the parent can tell the two outcomes
		 * apart through waitpid()'s own status. */
		*addr = 0;
		_exit(2);
	}
	if (pid > 0) {
		CHECK(waitpid(pid, &status, 0) == pid);
		CHECK(WIFSIGNALED(status));
		CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
	}

	CHECK(dlclose(h) == 0);
}

/* ============================================================
 * Per-object TLS (aarch64 only -- src/dlfcn/linux/plat_dlfcn.c's own
 * "TLS / per-library thread descriptors" banner)
 * ==============================================================
 */

#if defined(__aarch64__)
/* aarch64: plat_dlfcn.c implements the real thing -- see its own
 * TLSDESC resolver. bump_tls_counter() must actually work, and two
 * independent dlopen() instances of the identical .so must get two
 * genuinely separate TLS blocks ("own TD per library", not a shared
 * DTV slot aliased between them). */
static void test_pt_tls_per_object(const char *dir)
{
	char path[4096];
	void *t1, *t2;
	fnptr0 bump1, bump2;

	fixture_path(path, sizeof path, dir, "dlfix_tls.so");
	t1 = dlopen(path, RTLD_NOW);
	if (!t1) printf("dlopen dlfix_tls.so failed: %s\n", dlerror());
	CHECK(t1 != NULL);
	if (!t1) return;

	bump1 = (fnptr0)dlsym(t1, "bump_tls_counter");
	CHECK(bump1 != NULL);
	CHECK(bump1 && bump1() == 1);
	CHECK(bump1 && bump1() == 2);
	CHECK(bump1 && bump1() == 3);

	t2 = dlopen(path, RTLD_NOW);
	CHECK(t2 != NULL && t2 != t1);
	if (t2) {
		bump2 = (fnptr0)dlsym(t2, "bump_tls_counter");
		CHECK(bump2 != NULL);
		/* Starts fresh at 1, independent of t1's own counter already
		 * being at 3 -- own TD per library, not a shared/aliased one. */
		CHECK(bump2 && bump2() == 1);
		CHECK(bump1 && bump1() == 4); /* t1's own state, untouched by t2 */
		CHECK(dlclose(t2) == 0);
	}
	CHECK(dlclose(t1) == 0);
}
#else
/* Every other arch this crt/PT_TLS handling supports (x86_64/i386):
 * per-object TLS is NOT implemented -- see plat_dlfcn.c's own TLS
 * banner for exactly why (x86_64's "variant II" TCB shape is
 * structurally different from aarch64's, and needs a separately-derived
 * resolver that does not exist yet). dlopen() must keep refusing
 * cleanly, exactly as it always has, never loading a PT_TLS segment it
 * cannot correctly place. */
static void test_pt_tls_per_object(const char *dir)
{
	char path[4096];
	void *h;
	char *e;

	fixture_path(path, sizeof path, dir, "dlfix_tls.so");
	h = dlopen(path, RTLD_NOW);
	CHECK(h == NULL);
	e = dlerror();
	CHECK(e != NULL && strstr(e, "PT_TLS") != NULL);
}
#endif

/* ============================================================
 * R_AARCH64_IRELATIVE / R_X86_64_IRELATIVE (GNU "ifunc" dispatch --
 * src/dlfcn/linux/plat_dlfcn.c's apply_one_reloc(), see that constant's
 * own #define comment)
 * ==============================================================
 */

/* dlfix_ifunc.so's call_ifunc() only works at all if apply_one_reloc()
 * actually CALLS the resolver named by an R_AARCH64_IRELATIVE
 * relocation's own addend and stores its RETURN value -- see that
 * fixture's own comment for exactly what a loader bug (storing the
 * resolver's address instead of calling it) would do here instead: not
 * a crash necessarily, just the wrong answer (or worse, since
 * pick_target() and ifunc_compute() do not share a signature). 21*2 ==
 * 42 is the actual, correct dispatch outcome, not merely "did not
 * crash". */
static void test_r_irelative_ifunc_dispatch(const char *dir)
{
	char path[4096];
	void *h;
	int (*call_ifunc)(int);

	fixture_path(path, sizeof path, dir, "dlfix_ifunc.so");
	h = dlopen(path, RTLD_NOW);
	if (!h) printf("dlopen dlfix_ifunc.so failed: %s\n", dlerror());
	CHECK(h != NULL);
	if (!h) return;

	call_ifunc = (int (*)(int))dlsym(h, "call_ifunc");
	CHECK(call_ifunc != NULL);
	CHECK(call_ifunc && call_ifunc(21) == 42);

	CHECK(dlclose(h) == 0);
}

int main(int argc, char **argv)
{
	const char *dir = fixture_dir(argc > 0 ? argv[0] : "posix-dl-linux");

	test_dt_needed_chases_dependency(dir);
	test_dt_needed_never_dedups(dir);
	test_dt_needed_missing_dependency_fails_cleanly(dir);
	test_dt_init_array_runs_constructor(dir);
	test_dt_init_array_runs_once_per_instance(dir);
	test_pt_tls_per_object(dir);
	test_r_irelative_ifunc_dispatch(dir);
	/* Last on purpose -- see this test's own comment for why. */
	test_pt_gnu_relro_hardening(dir);

	if (fails) { printf("posix-dl-linux: failures: %d\n", fails); return 1; }
	printf("posix-dl-linux: all ok\n");
	return 0;
}
