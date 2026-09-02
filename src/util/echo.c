/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * echo(1p): `echo [string...]`
 *
 * DESCRIPTION: "The echo utility shall write its arguments to standard
 * output, followed by a <newline>.  If there are no arguments, only the
 * <newline> is written."
 *
 * ---- Why this file does less than a shell's `echo` usually does -------
 *
 * OPERANDS: "If the first operand is -n, or if any of the operands
 * contain a <backslash> character, the results are implementation-
 * defined" -- and APPLICATION USAGE spells out just how wide that split
 * is: XSI systems expand backslash escapes (\a \b \c \f \n \r \t \v \\
 * and \0num) unconditionally, csh-descended shells never do, and still
 * others gate escape expansion behind an explicit -e.  There is no
 * reading of the base standard that picks one of these for you.
 *
 * Per this project's "refuse rather than guess" rule (see bi_set's own
 * comment in src/sh/builtin.c for the same reasoning applied to shell
 * options), this implementation does not guess at XSI's -e semantics or
 * silently pick one shell's convention: backslash sequences are never
 * interpreted here, in any mode -- a literal `\n` in an argument prints
 * as the two characters backslash-n, never a newline.  There is no -e
 * option at all, so there is nothing to refuse; the base, unspecified-
 * free case (backslash-free arguments) is implemented exactly as
 * DESCRIPTION says.
 *
 * -n (suppress the trailing <newline>) is implemented despite being
 * technically in "implementation-defined" territory too, because unlike
 * the backslash question it is *not* actually contested in practice:
 * every shell this project's own bootstrap chain has to interoperate
 * with (dash, bash, the historical Bourne/System V echo) treats a sole
 * leading -n the same way, and APPLICATION USAGE names it explicitly as
 * the one implementation-defined case worth calling out by name.  A
 * script that wants byte-for-byte portability should use printf(1p)
 * instead, per that same section -- this project does not pretend
 * `echo` can be made portable by implementing more of it.
 *
 * EXIT STATUS: "0 Successful completion.  >0 An error occurred." -- a
 * write failure (closed stdout, full pipe partner gone, etc.) is the
 * only error case that can occur here.
 *
 * Spec consulted: https://pubs.opengroup.org/onlinepubs/9699919799/utilities/echo.html
 */
#include <string.h>
#include <stdio.h>
#include "util.h"

int __util_echo_main(
	int argc, char **argv elements_withtok(null_terminated, argc))
{
	int i = 1;
	int suppress_nl = 0;
	int first = 1;

	if (argc > 1 && !strcmp(argv[1], "-n")) {
		suppress_nl = 1;
		i = 2;
	}

	for (; i < argc; i++) {
		if (!first) {
			if (fputc(' ', stdout) == EOF) return 1;
		}
		if (fputs(argv[i], stdout) == EOF) return 1;
		first = 0;
	}
	if (!suppress_nl) {
		if (fputc('\n', stdout) == EOF) return 1;
	}
	if (fflush(stdout) == EOF) return 1;
	return 0;
}
