/*
 * SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Replacements for the two helpers whose upstream implementation cannot do
 * its job against this library.
 *
 * Which two, and why each is here rather than upstream's version:
 *
 *   t_setrlim  upstream setrlim.c compiles (we do ship
 *              <sys/resource.h>), but setrlimit() here does not enforce
 *              anything -- see src/misc/resource.c -- so "the limit is
 *              now N" is not a fact the caller may rely on.
 *   t_fdfill   upstream fdfill.c dup()s until EMFILE.  With no
 *              enforced RLIMIT_NOFILE that loop does not terminate on
 *              an error, it terminates on memory exhaustion, which is
 *              not the same experiment.
 *
 * NO LONGER HERE: t_setutf8.  Upstream utf8.c needs <langinfo.h> and
 * nl_langinfo(), which this library did not have when this file was
 * written and now does (include/langinfo.h, src/misc/langinfo.c), so
 * tools/libc-test.sh builds upstream's helper again and this file has
 * no business substituting for it.  A stub is only defensible while the
 * real thing cannot be built; the moment it can, keeping the stub would
 * park tests as unverified that are now genuinely adjudicable.
 *
 * WHY THIS FILE LIVES ONE DIRECTORY DOWN
 *
 * It has no main() and is not a test.  The Makefile's TEST_SRCS is
 * `wildcard test/*.c` and its pattern rule links every match into its own
 * PE; tools/asan-build.sh and tools/lint.sh's lint-unreferenced stage
 * make the same assumption.  Same reason test/rpath-plugin-src/ exists,
 * and the Makefile says so at obj/test/rpath-plugin.dll: a non-test .c
 * file belongs out of that glob entirely rather than on three separate
 * exception lists.  (Observed, once: putting it in test/ turned three
 * gate stages red at the same time.)
 *
 * The honesty rule, and the whole reason this file is not four empty
 * functions: a stubbed helper must be *visible in the test's own
 * output at run time*, not inferred from a static list somewhere else.
 * Each stub prints one line beginning with the marker below, and
 * tools/libc-test.sh turns any test whose output contains that marker
 * into rc=77 ("unverified"), never a pass -- the same rule it applies
 * to Wine's RtlCloneUserProcess abort, and for the same reason: on real
 * Windows, or on a future build of this library that has enforced
 * rlimits, these tests must be able to become real passes or
 * real failures without anyone editing an exclusion list.
 *
 * The marker text is load-bearing.  tools/libc-test.sh greps for it
 * literally and its self-test asserts that it is present here; changing
 * it in one place without the other fails that self-test rather than
 * silently promoting two unverifiable tests to passes.
 */
#include <stdint.h>
#include <stddef.h>
#include "test.h"

#define SHIM_MARK "SHIM-STUBBED: "

int t_setrlim(int r, long lim)
{
	(void)r; (void)lim;
	t_printf(SHIM_MARK "t_setrlim: setrlimit() is not enforced on this"
	    " target (src/misc/resource.c); no limit was applied\n");
	return -1;
}

void t_fdfill(void)
{
	t_printf(SHIM_MARK "t_fdfill: no enforced RLIMIT_NOFILE on this"
	    " target; the descriptor table was NOT filled\n");
}
