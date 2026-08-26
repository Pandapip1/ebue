/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Locating the command interpreter that POSIX's [ENOEXEC] fallback runs.
 *
 * Two independent clauses need the same answer, so they share this file
 * rather than each picking a shell of its own:
 *
 *   - XSH exec.html DESCRIPTION: "In the cases where the other members
 *     of the exec family of functions would fail and set errno to
 *     [ENOEXEC], the execlp() and execvp() functions shall execute a
 *     command interpreter and the environment of the executed command
 *     shall be as if the process invoked the sh utility using execl()
 *     as follows:
 *
 *         execl(<shell path>, arg0, file, arg1, ..., (char *)0);
 *
 *     where <shell path> is an unspecified pathname for the sh
 *     utility".  (POSIX.1-2017; POSIX.1-2024 writes the same line with
 *     <name> in place of arg0 and calls it "an unspecified string".)
 *
 *   - XCU 2.9.1 Command Search and Execution: "If the execl() function
 *     fails due to an error equivalent to the [ENOEXEC] error ... the
 *     shell shall execute a command equivalent to having a shell
 *     invoked with the pathname resulting from the search as its first
 *     operand".
 *
 * Why this matters more here than on Unix.  XRAT (XCU C.2.9.1) records
 * the requirement as deliberate protection for exactly this platform:
 * it "requires that the shell can execute shell scripts directly, even
 * if the underlying system does not support the common #! interpreter
 * convention".  NT does not.  RtlCreateUserProcess answers
 * STATUS_INVALID_IMAGE_NOT_MZ / STATUS_INVALID_IMAGE_FORMAT for a
 * script, which src/process/spawn.c turns into ENOEXEC -- so without
 * the fallback a shell script cannot be executed on this system at all,
 * by any route.  This is the mechanism, not a conformance nicety.
 *
 * ---- Which sh, and why in this order --------------------------------
 *
 * "<shell path> is an unspecified pathname", so the choice is ours to
 * make and to justify.  What is chosen is `sh.exe`, this repository's
 * own sh(1p) (sh/main.c over the engine in src/sh/), looked for in two
 * places, in this order:
 *
 *   1. The directory of the calling image, from __progname_full
 *      (crt1.c: ImagePathName).  This is the installed layout -- the
 *      Makefile puts sh.exe and every other program in the same
 *      $bindir -- and it is the answer that cannot be steered by
 *      anything outside the caller's own installation directory, which
 *      is the same threat model include/ntlibc/rpath.h states for
 *      $ORIGIN resolution.  It is also self-evidently right for the
 *      one caller that *is* a shell: sh.exe finds itself.
 *
 *   2. Failing that, "sh" through __find_program(), i.e. a PATH search
 *      with the .exe suffix appended (src/process/find_program.c).
 *      Needed because "beside the caller" is false in every layout that
 *      is not `make install` -- in this tree the obj/test programs and
 *      obj/sh/sh.exe are siblings of nothing -- and because a Windows
 *      deployment that puts its tools on PATH rather than in one
 *      directory is ordinary, not exotic.
 *
 *      PATH is consulted second rather than first precisely because it
 *      is the steerable one.  Note what the exposure actually is: the
 *      only callers of this are execvp()/execlp() and the shell's own
 *      command search, both of which have *already* found the program
 *      they were asked to run by searching that same PATH.  A PATH an
 *      attacker controls has decided which program runs before it ever
 *      gets to decide which sh interprets it.
 *
 * What is deliberately *not* consulted:
 *
 *   - %ComSpec%.  That names cmd.exe, and src/stdlib/system.c uses it
 *     for exactly that reason -- system() is specified in terms of "a
 *     command processor", implementation-defined, so honouring the
 *     Windows convention there is right.  These two clauses are not:
 *     both name the sh utility, whose language is XCU 2, and cmd.exe
 *     does not implement it.  Running a `#!/bin/sh` script through
 *     cmd.exe is the silent-misreading failure mode, not a fallback.
 *
 *   - $CONFIG_SHELL / $SHELL.  CONFIG_SHELL is autoconf's private
 *     variable, not a POSIX interface; $SHELL is XBD 8.3's "pathname of
 *     the user's preferred command language interpreter", which is
 *     about interactive login, and neither clause above mentions it.
 *     Letting an environment variable redirect the interpreter for
 *     every script a program execs is a larger and less obvious
 *     behaviour change than either clause asks for.
 *
 * Returns a malloc'd path, or 0 (errno unspecified) if no sh could be
 * found.  Callers restore their own errno; see src/process/exec.c.
 */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libc.h"

char *__find_interpreter(void)
{
	static const char base[] = "sh.exe";
	size_t n, i;
	char *p;

	/* 1. Beside the calling image.  The trailing separator is kept
	 * rather than stripped and re-added, so "C:\prog.exe" gives
	 * "C:\sh.exe" and not "C:sh.exe"; src/internal/rpath.c's
	 * image_dir() has to strip it because it joins arbitrary __rpath
	 * entries onto the result, and this has exactly one name to
	 * append.  Its cached copy is deliberately not reused: that would
	 * drag rpath.o into every program that calls a p-form for eleven
	 * lines of string handling.
	 *
	 * access(F_OK), not X_OK: on NTFS every regular file satisfies
	 * X_OK (src/stat/chmod.c -- there is no execute bit to test), so
	 * X_OK would answer a question this cannot use.  What is being
	 * asked is only "is there a file here at all", so that a missing
	 * sh.exe falls through to the PATH search below instead of being
	 * handed to execve() and coming back ENOENT. */
	if (__progname_full) {
		n = strlen(__progname_full);
		for (i = n; i > 0 && __progname_full[i-1] != '\\' && __progname_full[i-1] != '/'; i--) ;
		if (i > 0 && (p = malloc(i + sizeof base)) != 0) {
			memcpy(p, __progname_full, i);
			memcpy(p + i, base, sizeof base);
			if (access(p, F_OK) == 0) return p;
			free(p);
		}
	}

	/* 2. PATH. */
	return __find_program("sh", 1);
}
