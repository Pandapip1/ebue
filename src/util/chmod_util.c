/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Named chmod_util.c, not chmod.c -- see src/util/mkdir_util.c's own
 * comment on exactly why (src/stat/chmod.c already owns that basename,
 * and `ar` truncates member names to a bare, path-less basename).
 *
 * chmod(1p): `chmod mode file...`
 *
 * OPERANDS:
 *  mode  "Represents the change to be made to the file mode bits of each
 *         file named by one of the file operands."  Octal or symbolic --
 *         see src/util/modeparse.h for the exact grammar implemented and
 *         its documented gap (X/s/t/permcopy are refused, not
 *         approximated).
 *  file  "A pathname of a file whose file mode bits shall be modified."
 *
 * EXIT STATUS: "0 The utility executed successfully and all requested
 * changes were made." ">0 An error occurred." -- diagnose-and-continue,
 * same shape as this project's other utilities.
 *
 * A symbolic mode's '+'/'-'/'=' are relative to *this file's own current
 * mode bits*, unlike mkdir(1p)/mkfifo(1p)'s -m (which assume a=rwx or
 * a=rw since there is no existing file yet) -- so, unlike those two,
 * this utility has to stat() each file before it can even parse the
 * mode operand for it, and does so once per file rather than once
 * overall.
 *
 * -R (recurse into directories) is a real chmod(1p) option that is not
 * implemented here -- refused with a diagnostic rather than silently
 * walking (or not walking) a tree the caller asked it to.  The single-
 * file, non-recursive form is the one this project's own bootstrap
 * scripts need; -R is a real, tracked gap, not an oversight.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#include "libc.h"
#include "util.h"
#include "modeparse.h"

int __util_chmod_main(
	int argc, char **argv elements_withtok(null_terminated, argc))
{
	int i, fail = 0;
	const char *mode_spec;

	if (argc > 1 && !strcmp(argv[1], "-R")) {
		__util_diagf("chmod: -R: not implemented -- see src/util/chmod_util.c\n");
		return 1;
	}
	if (argc < 3) {
		__util_diagf("chmod: missing operand\n");
		return 1;
	}
	mode_spec = argv[1];

	for (i = 2; i < argc; i++) {
		struct stat st;
		mode_t newmode;

		if (stat(argv[i], &st) != 0) {
			__util_diagf("chmod: %s: %s\n", argv[i], strerror(errno));
			fail = 1;
			continue;
		}
		/* The who-omitted umask rule (modeparse.h) is chmod(1p)'s
		 * own OPERANDS text, not something mkdir(1p)/mkfifo(1p) add
		 * on top of it -- so a bare `chmod +w file` is exactly as
		 * umask-sensitive here as `mkdir -m +w newdir` is. */
		if (__util_parse_mode("chmod", mode_spec, st.st_mode & 07777,
		                      (mode_t)__umask_get(), &newmode) < 0)
			return 1; /* malformed mode operand: usage error, not per-file */
		if (chmod(argv[i], newmode) != 0) {
			__util_diagf("chmod: %s: %s\n", argv[i], strerror(errno));
			fail = 1;
		}
	}
	return fail;
}

// NOLINTEND(misc-include-cleaner)
