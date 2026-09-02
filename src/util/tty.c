/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * tty(1p): "write to the standard output the name of the terminal
 * that is open as standard input.  The name that is used shall be
 * equivalent to the string that would be returned by the ttyname()
 * function" (tty.html DESCRIPTION).  SYNOPSIS is bare "tty" -- OPERANDS
 * is "None." -- and STDOUT is exactly:
 *
 *   "%s\n", <terminal name>            -- stdin is a terminal
 *   "not a tty\n"                      -- stdin is not (POSIX locale)
 *
 * EXIT STATUS (tty.html, verbatim): 0 stdin is a terminal; 1 stdin is
 * not a terminal; >1 an error occurred.
 *
 * -s: NOT in the current (Issue 7, IEEE Std 1003.1-2017) tty.html --
 * that page's own CHANGE HISTORY says outright "The obsolescent -s
 * option is removed" as of Issue 6, and OPTIONS there is just "The tty
 * utility shall conform to XBD Utility Syntax Guidelines" with no
 * flags of its own.  Implemented anyway, as a real, cited extension
 * (not a misreading of the current spec): every common tty(1)
 * -- GNU coreutils, the *BSDs -- still ships a -s ("silent"/"quiet":
 * suppress the name/"not a tty" line, report only through the exit
 * status) as the direct descendant of the pre-Issue-6 POSIX option of
 * the same name and meaning, and this project's own explicit direction
 * for this task is to build it.  Same shape as src/util/timeout.c's
 * own header comment justifying a utility this project implements
 * beyond (there: outside) what XCU strictly mandates.
 *
 * ttyname()'s own real/N/A split is entirely src/unistd/ttyname.c's
 * concern (a fixed "CON" answer once isatty() says yes) -- this file
 * just calls it and reports what it says, never fabricating a name of
 * its own.
 *
 * Like every other __util_<name>_main(), never calls exit()/_exit():
 * it also runs in-process as the `tty` shell built-in (src/sh/
 * builtin.c's bi_tty()) -- see src/internal/util.h's own header
 * comment and src/util/dd.c's for the established reasoning.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "util.h"

int __util_tty_main(int argc, char **argv)
{
	int silent = 0;
	int i;
	char *name;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-s")) { silent = 1; continue; }
		__util_diagf("tty: %s: invalid option\n", argv[i]);
		return 2;
	}

	errno = 0;
	name = ttyname(0);
	if (name) {
		if (!silent && printf("%s\n", name) < 0) {
			__util_diagf("tty: write error\n");
			return 2;
		}
		return 0;
	}

	if (errno == ENOTTY) {
		if (!silent && printf("not a tty\n") < 0) {
			__util_diagf("tty: write error\n");
			return 2;
		}
		return 1;
	}

	/* Anything else (e.g. a genuinely bad fd 0) is a real error, not
	 * the ordinary "not a tty" outcome -- tty.html's own ">1" bucket. */
	__util_diagf("tty: %s\n", strerror(errno));
	return 2;
}
