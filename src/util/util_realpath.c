/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Named util_realpath.c, not realpath.c: same tcc-`ar` 15-byte
 * member-name collision with src/stdlib/realpath.c (the realpath(3)
 * library function this file calls) that src/util/util_basename.c's
 * header explains in full -- the util_ prefix is the whole fix, the
 * exported symbol is still __util_realpath_main().
 *
 * realpath -- like readlink (src/util/readlink.c's own comment), not an
 * XCU mandatory utility, folded into this tier by this project's own
 * POSIX-utilities plan for the same bootstrap reason.  `realpath
 * path...` prints each path's canonicalized absolute form, one per
 * line, via the realpath() library call (src/stdlib/realpath.c) --
 * resolving `.`, `..` and symlinks the way every real GNU/BSD
 * realpath(1) does with no options.
 *
 * Multiple operands are each processed in turn rather than stopping at
 * the first failure, so one bad path (one that does not resolve to an
 * existing file -- realpath(3)'s ENOENT) does not silently swallow the
 * rest of the command line's operands; the invocation as a whole still
 * reports failure via a nonzero exit status.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "util.h"

int __util_realpath_main(int argc, char **argv)
{
	int i, status = 0;

	if (argc < 2) {
		fprintf(stderr, "realpath: missing operand\n");
		return 2;
	}

	for (i = 1; i < argc; i++) {
		/* realpath(path, 0): "If resolved_path is a null pointer, ...
		 * space ... shall be allocated as necessary" -- src/stdlib/
		 * realpath.c implements exactly this NULL-buffer form. */
		char *r = realpath(argv[i], 0);
		if (!r) {
			fprintf(stderr, "realpath: %s: %s\n", argv[i], strerror(errno));
			status = 1;
			continue;
		}
		fputs(r, stdout);
		fputc('\n', stdout);
		free(r);
	}
	return status;
}
