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

/* ================= island: <tar.h>, alone ======================== */
#if 0 /* UNIMPL: tar.h.html DESCRIPTION: "The <tar.h> header shall define
	the following symbolic constants with the indicated values",
	listing TMAGIC "ustar" / TMAGLEN 6 / TVERSION "00" / TVERSLEN 2,
	the nine typeflag values REGTYPE..CONTTYPE, and the twelve octal
	mode-field bits TSUID..TOEXEC.  ntlibc has NO <tar.h> at all:
	include/ does not contain the file.  Triage: ABSENT (the whole
	header).  <tar.h> is POSIX base -- its SYNOPSIS box carries no
	option-group margin marker; the only [XSI] in the page is on the
	single constant TSVTX.

	WHY THIS ONE PROVES THE SHAPE OF THE BLIND SPOT group U audits.
	<tar.h> declares no functions whatsoever.  It is therefore outside
	test/POSIX-GAP-ACCOUNTING.md's 1177 POSIX.1-2017 function
	interfaces BY CONSTRUCTION rather than by oversight -- no
	function-granular accounting, however exhaustive, can ever record
	its absence.  Before this fence, nothing in the tree recorded that
	ntlibc lacks a mandatory POSIX header: not the ledger, not the
	header inventory, not a banner.  It read as "fine".

	ACCEPTANCE CRITERION: the header, with the values the standard
	prints.  Nothing more -- this is a pure constants header, there is
	no behaviour behind it and nothing in src/ would need to change.
	Consumer: GNU tar and pax read the ustar typeflags and magic from
	here rather than defining their own.

	Observed today: fails to COMPILE, "include file 'tar.h' not
	found" -- a compile-time failure, so no Wine-vs-real-NT
	uncertainty arises. */
#include <tar.h>

static void test_tar_h_constants(void)
{
	/* "TMAGIC ... Used in the magic field in the ustar header block,
	 * INCLUDING the trailing null byte"; "TMAGLEN 6 ... Length in
	 * octets of the magic field." */
	UCHECK(sizeof TMAGIC == (unsigned)TMAGLEN);
	UCHECK(TMAGIC[0] == 'u' && TMAGIC[1] == 's' && TMAGIC[2] == 't');
	UCHECK(TMAGIC[3] == 'a' && TMAGIC[4] == 'r' && TMAGIC[5] == '\0');

	/* "TVERSION "00" ... EXCLUDING the trailing null byte";
	 * "TVERSLEN 2". */
	UCHECK(sizeof TVERSION - 1 == (unsigned)TVERSLEN);
	UCHECK(TVERSION[0] == '0' && TVERSION[1] == '0');

	/* Typeflag field definitions -- the values are the archive
	 * format's own bytes, so they are exact, not floors. */
	UCHECK(REGTYPE == '0');
	UCHECK(AREGTYPE == '\0');
	UCHECK(LNKTYPE == '1');
	UCHECK(SYMTYPE == '2');
	UCHECK(CHRTYPE == '3');
	UCHECK(BLKTYPE == '4');
	UCHECK(DIRTYPE == '5');
	UCHECK(FIFOTYPE == '6');
	UCHECK(CONTTYPE == '7');

	/* Mode field bit definitions (octal). */
	UCHECK(TSUID == 04000);
	UCHECK(TSGID == 02000);
	UCHECK(TSVTX == 01000);		/* [XSI] */
	UCHECK(TUREAD == 00400);
	UCHECK(TUWRITE == 00200);
	UCHECK(TUEXEC == 00100);
	UCHECK(TGREAD == 00040);
	UCHECK(TGWRITE == 00020);
	UCHECK(TGEXEC == 00010);
	UCHECK(TOREAD == 00004);
	UCHECK(TOWRITE == 00002);
	UCHECK(TOEXEC == 00001);
}
#endif

/* ================= island: <cpio.h>, alone ======================= */
#if 0 /* UNIMPL: cpio.h.html DESCRIPTION: "The <cpio.h> header shall
	define the symbolic constants needed by the c_mode field of the
	cpio archive format, with the names and values given in the
	following table", listing the twenty C_* octal constants, followed
	by "The <cpio.h> header shall define the following symbolic
	constant as a string: MAGIC "070707"".  ntlibc has NO <cpio.h> at
	all.  Triage: ABSENT (the whole header).

	POSIX BASE, NOT XSI -- correcting the obvious assumption: the
	page's own CHANGE HISTORY says "Issue 7 The <cpio.h> header is
	moved from the XSI option to the Base."  It was XSI in Issue 6 and
	is not any more, so its absence is a base-conformance hole and not
	a missing option group.  That distinction is the one
	test/POSIX-GAP-ACCOUNTING.md calls "not cosmetic", and it may
	decide whether this header is ever worth adding.

	Like <tar.h>, this header declares no functions, so it is outside
	the 1177-interface accounting by construction; see the <tar.h>
	fence above for why that is the whole point of group U.

	ACCEPTANCE CRITERION: the header, with the values the standard
	prints.  Pure constants; nothing in src/ would change.

	Observed today: fails to COMPILE, "include file 'cpio.h' not
	found". */
#include <cpio.h>

static void test_cpio_h_constants(void)
{
	UCHECK(C_IRUSR == 0000400);
	UCHECK(C_IWUSR == 0000200);
	UCHECK(C_IXUSR == 0000100);
	UCHECK(C_IRGRP == 0000040);
	UCHECK(C_IWGRP == 0000020);
	UCHECK(C_IXGRP == 0000010);
	UCHECK(C_IROTH == 0000004);
	UCHECK(C_IWOTH == 0000002);
	UCHECK(C_IXOTH == 0000001);
	UCHECK(C_ISUID == 0004000);
	UCHECK(C_ISGID == 0002000);
	UCHECK(C_ISVTX == 0001000);
	UCHECK(C_ISDIR == 0040000);
	UCHECK(C_ISFIFO == 0010000);
	UCHECK(C_ISREG == 0100000);
	UCHECK(C_ISBLK == 0060000);
	UCHECK(C_ISCHR == 0020000);
	UCHECK(C_ISCTG == 0110000);
	UCHECK(C_ISLNK == 0120000);
	UCHECK(C_ISSOCK == 0140000);

	/* "shall define the following symbolic constant as a string:
	 * MAGIC "070707"" -- six digits plus the terminating null. */
	UCHECK(sizeof MAGIC == 7);
	UCHECK(MAGIC[0] == '0' && MAGIC[1] == '7' && MAGIC[2] == '0');
	UCHECK(MAGIC[3] == '7' && MAGIC[4] == '0' && MAGIC[5] == '7');
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
