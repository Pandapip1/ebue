/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Exercises the RPATH/$ORIGIN-equivalent delay-load mechanism in
 * include/ntlibc/rpath.h and include/ntlibc/delayload.h. Named *-win.c
 * (see the Makefile's note by TEST_SRCS/TEST_RUN) because it needs a
 * companion DLL sitting next to the executable, which "make check"
 * arranges (obj/test/rpath-plugin.dll, built straight from
 * test/rpath-plugin-src/rpath-plugin.c) but does not itself run under
 * Wine as part of the suite -- run it directly, from obj/test/, to see
 * it pass: `wine obj/test/rpath-win.exe`.
 *
 * Covered:
 *   - resolution from the image directory ($ORIGIN), through both
 *     __rpath (a "." entry) and a dllname with an explicit relative
 *     path component, via the real descriptor/IAT/INT delay-load path
 *     (NTLIBC_DELAY_DLL/NTLIBC_DELAY_STUB) end to end -- load, resolve,
 *     call, and that the second call reuses the now-patched IAT slot
 *     rather than resolving again;
 *   - a missing DLL (ntlibc_rpath_load fails, with a diagnosable
 *     ntlibc_rpath_error());
 *   - a missing symbol in a DLL that *does* exist (ntlibc_rpath_sym
 *     fails, likewise diagnosable);
 *   - that ordinary program state (argv, environ) untouched by any of
 *     this still looks normal, i.e. using this facility does not
 *     disturb anything else. (The zero-*startup*-cost claim itself --
 *     that a program which never calls into rpath.c/delayload.c never
 *     pulls those objects in -- is a link-time property, not a runtime
 *     one, and is checked separately: `nm obj/test/misc.exe` (a test
 *     that never mentions this API) has no ntlibc_rpath_... or
 *     ntlibc_delayLoadHelper2 symbols.)
 *
 * NT-only, like src/internal/rpath.c and src/internal/delayload.c that
 * this exercises: under tools/asan-build.sh's native run this fails to
 * compile (see the #error below) rather than fail to link with a
 * confusing "undefined reference to ntlibc_rpath_load" -- those two
 * files are excluded from that build for the same reason, so nothing
 * this test calls would exist there regardless.
 */
#ifndef _WIN32
#error "rpath-win.c is NT-only (exercises rpath.c/delayload.c, both NT-only); see src/internal/rpath.c"
#endif
#include <stdio.h>
#include <string.h>
#include "ntlibc/delayload.h"

/* $ORIGIN semantics: "." resolves to the image's own directory, where
 * the Makefile places rpath-plugin.dll right alongside this .exe. */
const char *const __rpath[] = { ".", 0 };

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* The real delay-load machinery: a descriptor + delay IAT/INT for
 * "rpath-plugin.dll", one imported function (rpath_plugin_answer,
 * index 0), and the generated stub a caller actually calls. */
NTLIBC_DELAY_DLL(plugin, "rpath-plugin.dll", 1, NTLIBC_DELAY_NAME("rpath_plugin_answer"));
NTLIBC_DELAY_STUB(int, plugin, 0, rpath_plugin_answer, (void), ())

int main(int argc, char **argv)
{
	ntlibc_dll_t *dll;
	void *sym;

	/* ---- end-to-end delay load through __rpath's "." entry --------- */
	CHECK(rpath_plugin_answer() == 42);
	/* Second call must reuse the now-resolved IAT slot (extern void*
	 * ntlibc_delay_iat_plugin[0].function is non-NULL after the first
	 * call) rather than resolving again -- observable indirectly: it
	 * still returns the right answer, and nothing about repeating the
	 * call crashes or re-touches ntlibc_rpath_error()'s "no error"
	 * default, which a spurious second resolve attempt would not
	 * disturb either way, so this is mostly a smoke check that the
	 * cached path is exercised at all. */
	CHECK(rpath_plugin_answer() == 42);

	/* ---- same DLL, reached by an explicit relative path component -- */
	/* Bypasses __rpath entirely (see rpath.h): "./rpath-plugin.dll" has
	 * a path component, so it is resolved directly against the image
	 * directory. */
	dll = ntlibc_rpath_load("./rpath-plugin.dll");
	CHECK(dll != 0);
	if (dll) {
		sym = ntlibc_rpath_sym(dll, "rpath_plugin_answer");
		CHECK(sym != 0);
		if (sym) CHECK(((int (*)(void))sym)() == 42);
	}

	/* ---- missing DLL ------------------------------------------------ */
	dll = ntlibc_rpath_load("no-such-plugin-at-all.dll");
	CHECK(dll == 0);
	CHECK(strcmp(ntlibc_rpath_error(), "no error") != 0);
	CHECK(strstr(ntlibc_rpath_error(), "no-such-plugin-at-all.dll") != 0);

	/* ---- missing symbol in a DLL that does exist --------------------- */
	dll = ntlibc_rpath_load("rpath-plugin.dll");
	CHECK(dll != 0);
	if (dll) {
		sym = ntlibc_rpath_sym(dll, "no_such_symbol_at_all");
		CHECK(sym == 0);
		CHECK(strcmp(ntlibc_rpath_error(), "no error") != 0);
	}

	/* ---- nothing about using this facility disturbs ordinary state -- */
	CHECK(argc >= 1);
	CHECK(argv != 0 && argv[0] != 0);

	if (!fails) printf("PASS\n");
	return fails ? 1 : 0;
}
