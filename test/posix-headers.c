/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * XBD header-content conformance, for the clauses that can only be
 * tested in ISOLATION -- audit group U; see test/POSIX-COVERAGE.md,
 * "XBD header contents -- the macros no ledger counts (group U)".
 *
 * THE RULE THIS FILE EXISTS FOR: a header-content fence must fail the
 * way a consumer fails.  Where an XBD clause is about what ONE header
 * supplies ON ITS OWN, the test needs a translation unit that includes
 * only that header -- otherwise the instrument fights the file it lives
 * in.  <fcntl.h>'s SEEK_* clause is the worked example: <stdio.h> and
 * <unistd.h> both define SEEK_SET, so in any ordinary test file the
 * clause is untestable and the best available fence degrades into a
 * runtime assertion about a recorded flag.  A real consumer meets this
 * as a COMPILE error in a single-header translation unit, and that is
 * what the fence here reproduces.  Where a clause is only about a name
 * existing SOMEWHERE, an existing test file is the right home and this
 * file is the wrong one.
 *
 * STRUCTURE, which is what makes the next such clause cheap: the top of
 * the file is a sequence of small TU-shaped "islands".  Each island
 * includes exactly the header its clause is about and nothing else, and
 * carries the test for that clause immediately after the #include, so
 * the test is compiled with only that header in scope.  Islands are
 * ordered so no earlier one can supply a name a later one probes.
 * Everything below the last island may include anything it likes.
 *
 * Because an island must not pull in <stdio.h>, the assertion helper is
 * forward-declared here and defined at the bottom of the file.
 */

static void u_check(int cond, const char *expr, int line);
#define UCHECK(c) u_check((c), #c, __LINE__)

/* ================= island: <fcntl.h>, alone ====================== */
#include <fcntl.h>

#if 0 /* UNIMPL: fcntl.h.html DESCRIPTION: "The <fcntl.h> header shall
	define the values used for l_whence, SEEK_SET, SEEK_CUR, and
	SEEK_END as described in <stdio.h>."  ntlibc defines all three in
	<stdio.h> and in <unistd.h> (checked, correct) but not in
	<fcntl.h>: include/fcntl.h neither defines them nor includes a
	header that does.  Triage: ABSENT.  The sentence is unconditional
	-- no option-group margin marker guards it -- and it exists
	precisely so a translation unit doing record locking, which needs
	<fcntl.h> for struct flock and F_SETLK, can fill in l_whence
	without also including <stdio.h>.  That is the real-world
	breakage, and it is why this test lives in an isolated island: on
	glibc and musl such a TU compiles, and here it does not.
	Nothing records the omission -- test/POSIX-GAP-ACCOUNTING.md
	enumerates the 1177 function interfaces and a symbolic constant is
	not one of them, and include/fcntl.h's banner does not mention it.
	Acceptance criterion, stated so it is not read wider than it is:
	<fcntl.h> defining the three values.  There is no behaviour behind
	this clause to implement -- lseek()/fcntl() already honour
	SEEK_SET/SEEK_CUR/SEEK_END, and are audited under priority 6.
	Observed today: fails to COMPILE in this island, "'SEEK_SET'
	undeclared" (verified by un-fencing and building with
	x86_64-win32-tcc; a compile-time failure, so no Wine-vs-real-NT
	uncertainty arises). */
static void test_fcntl_h_defines_seek_whence(void)
{
	struct flock fl;

	/* "as described in <stdio.h>": three distinct values ... */
	UCHECK(SEEK_SET != SEEK_CUR);
	UCHECK(SEEK_CUR != SEEK_END);
	UCHECK(SEEK_SET != SEEK_END);

	/* ... usable as l_whence, which is what the sentence is for. */
	fl.l_whence = SEEK_SET;
	UCHECK(fl.l_whence == SEEK_SET);
	fl.l_whence = SEEK_CUR;
	UCHECK(fl.l_whence == SEEK_CUR);
	fl.l_whence = SEEK_END;
	UCHECK(fl.l_whence == SEEK_END);
}
#endif

/* ============ end of the islands; anything goes below ============ */
#include <stdio.h>

static int fails;

static void u_check(int cond, const char *expr, int line)
{
	if (!cond) {
		fails++;
		printf("FAIL %s:%d: %s\n", __FILE__, line, expr);
	}
}

int main(void)
{
	/* Every clause in this file is currently fenced.  The binary
	 * exists so that un-fencing one is a one-line change and the
	 * harness picks it up with no Makefile edit (test/*.c is
	 * globbed).  u_check() is referenced by the fenced tests only,
	 * so touch it here to keep it honest. */
	u_check(1, "posix-headers harness is live", __LINE__);

	if (!fails) printf("posix-headers: all tests passed\n");
	return fails != 0;
}
