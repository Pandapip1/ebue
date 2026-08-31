/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * tools/linux-build-dlfcn-cross.sh's own test program -- the same
 * checks fuzz/linux_pilot_test_dlopen.c already proves for aarch64
 * (dlopen()/dlsym() against a real host-built .so, R_*_RELATIVE and
 * R_*_JUMP_SLOT/GLOB_DAT relocation proof, symbol resolution against
 * the static binary via dlopen(NULL,...), namespace isolation,
 * dlerror(), the PT_TLS refusal), just reported through __plat_write()
 * (src/internal/plat_fd.h) directly instead of stdio's printf() -- the
 * same reason fuzz/linux_pilot_test_crt.c's own report() avoids the
 * public write()/stdio front doors (see that file's own banner): this
 * curated cross build proves src/dlfcn/linux/plat_dlfcn.c's x86_64
 * relocation handling specifically, and pulling in the full stdio FILE/
 * stdout machinery (plus the socket-dispatch branch src/unistd/read.c's
 * own read() unconditionally references) is real, separate, unrelated
 * porting work this test does not need to drag in just to print PASS/
 * FAIL -- exactly the same scope line the CRT pilot already drew.
 *
 * A hand-rolled `contains()` stands in for <string.h>'s strstr() for
 * the same reason: pulling in strstr()/strchr()/strchrnul() is not
 * itself expensive, but keeping this file self-contained (no dependency
 * beyond <dlfcn.h> and plat_fd.h) keeps the curated FILES list in
 * tools/linux-build-dlfcn-cross.sh from growing for a check this file
 * can trivially do by hand.
 */
#include <dlfcn.h>
#include "plat_fd.h"

typedef int (*fnptr)(int);
typedef int (*fnptr0)(void);

/* __attribute__((used)): unlike fuzz/linux_pilot_test_dlopen.c's own
 * identical function (built without -Wl,--gc-sections -- see tools/
 * linux-build-dlfcn.sh), this cross build's own script DOES pass
 * --gc-sections (needed to drop the unused printf()/vprintf()/wprintf()
 * FILE-stream entry points and their own stdout/fflush dependencies --
 * see tools/linux-build-dlfcn-cross.sh's own banner). Nothing in the
 * static link graph itself ever calls host_provided_value() -- only the
 * dlopen()'d .so's own R_X86_64_JUMP_SLOT relocation reaches it, at
 * runtime, invisible to the linker's own reachability analysis -- so
 * without this attribute the linker would (and, caught empirically,
 * did) discard it as dead code before plat_dlfcn.c's own /proc/self/exe
 * symtab scan ever got a chance to find it. */
__attribute__((used))
int host_provided_value(void) { return 7; }

static int fails;

static size_t rawlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

static void report(int ok, const char *msg)
{
	__plat_handle_t out = (__plat_handle_t)(long)2; /* fd 1, boxed (fd+1) */
	if (!ok) fails++;
	__plat_write(out, ok ? "ok   - " : "FAIL - ", 7, 0);
	__plat_write(out, msg, rawlen(msg), 0);
	__plat_write(out, "\n", 1, 0);
}

/* Hand-rolled strstr() -- see this file's own banner for why. */
static int contains(const char *hay, const char *needle)
{
	size_t hn = rawlen(hay), nn = rawlen(needle);
	size_t i;
	if (nn == 0) return 1;
	if (nn > hn) return 0;
	for (i = 0; i + nn <= hn; i++) {
		size_t j;
		for (j = 0; j < nn && hay[i + j] == needle[j]; j++) {}
		if (j == nn) return 1;
	}
	return 0;
}

int main(void)
{
	void *h, *h2, *mh, *bad;
	fnptr0 dlso_answer, dlso_answer2, use_host_value;
	fnptr call_add_one;
	fnptr *table;
	fnptr0 self_host_value;
	char *e;

	/* ---- basic load + symbol resolution + relocation proof ------- */
	h = dlopen("./linux_pilot_test_dlopen_lib.so", RTLD_NOW);
	if (!h) {
		report(0, "dlopen() of the target .so failed");
		e = dlerror();
		report(e != 0, "dlerror() reported something");
		if (e) report(0, e);
		return 1;
	}
	report(1, "dlopen() of the target .so succeeded");

	dlso_answer = (fnptr0)dlsym(h, "dlso_answer");
	call_add_one = (fnptr)dlsym(h, "call_add_one");
	use_host_value = (fnptr0)dlsym(h, "use_host_value");
	table = (fnptr *)dlsym(h, "exported_table");

	report(dlso_answer && dlso_answer() == 42, "dlsym()'d dlso_answer() returns 42");
	report(call_add_one && call_add_one(5) == 7, "dlsym()'d call_add_one(5) returns 7");
	/* Proves R_X86_64_JUMP_SLOT resolution against THIS program's own
	 * symtab (host_provided_value is defined only here, never in the
	 * .so or in any host library -- the .so links against nothing). */
	report(use_host_value && use_host_value() == 107, "use_host_value() resolves against the static binary (R_X86_64_JUMP_SLOT)");
	/* Proves R_X86_64_RELATIVE: exported_table's own two entries are
	 * load-address-relative function pointers, fixed up at dlopen()
	 * time -- calling through them only works if that fixup happened. */
	report(table && table[0] && table[0](10) == 11, "exported_table[0](10) == 11 (R_X86_64_RELATIVE)");
	report(table && table[1] && table[1](10) == 12, "exported_table[1](10) == 12 (R_X86_64_RELATIVE)");

	report(dlclose(h) == 0, "dlclose() succeeded");

	/* ---- dlopen(NULL, ...) + dlsym: the main image's own symbols -- */
	mh = dlopen(0, RTLD_NOW);
	report(mh != 0, "dlopen(NULL, ...) succeeded");
	self_host_value = (fnptr0)dlsym(mh, "host_provided_value");
	report(self_host_value && self_host_value() == 7, "dlsym(dlopen(NULL,...), \"host_provided_value\") resolves and runs");

	/* ---- namespace isolation: two independent instances ----------- */
	h = dlopen("./linux_pilot_test_dlopen_lib.so", RTLD_NOW);
	h2 = dlopen("./linux_pilot_test_dlopen_lib.so", RTLD_NOW);
	report(h != 0 && h2 != 0, "two independent dlopen() calls both succeeded");
	report(h != h2, "no dedup -- two independent handles (see plat_dlfcn.c's own banner)");
	dlso_answer = (fnptr0)dlsym(h, "dlso_answer");
	dlso_answer2 = (fnptr0)dlsym(h2, "dlso_answer");
	report(dlso_answer != 0 && dlso_answer2 != 0, "dlsym() succeeded on both independent handles");
	report(dlso_answer != dlso_answer2, "genuinely separate mappings, not aliased");
	report(dlso_answer && dlso_answer() == 42, "first instance's dlso_answer() still returns 42");
	report(dlso_answer2 && dlso_answer2() == 42, "second instance's dlso_answer() also returns 42");
	report(dlclose(h) == 0, "dlclose() of the first instance succeeded");
	report(dlclose(h2) == 0, "dlclose() of the second instance succeeded");

	/* ---- dlerror(): single-shot front door over a sticky backend -- */
	bad = dlopen("./no-such-file.so", RTLD_NOW);
	report(bad == 0, "dlopen() of a nonexistent file fails");
	e = dlerror();
	report(e != 0 && contains(e, "no-such-file.so"), "dlerror() names the missing file");
	report(dlerror() == 0, "dlerror() is single-shot -- NULL immediately after");

	/* ---- PT_TLS: documented, disclosed refusal, not silent mis-load */
	bad = dlopen("./linux_pilot_test_dlopen_tlslib.so", RTLD_NOW);
	report(bad == 0, "dlopen() of a PT_TLS-bearing .so is refused");
	e = dlerror();
	report(e != 0 && contains(e, "PT_TLS"), "dlerror() names PT_TLS as the reason");

	if (fails) {
		report(0, "ONE OR MORE CHECKS FAILED");
		return 1;
	}
	report(1, "ALL DLOPEN TESTS PASS");
	return 0;
}
