/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * rmdir(1p): `rmdir [-p] dir...`
 *
 * DESCRIPTION: "The rmdir utility shall remove the directory entry
 * specified by each dir operand ... equivalent to the rmdir() function
 * called with the dir operand as its only argument."
 *
 * OPTIONS -p: "Remove all directories in a pathname.  For each dir
 * operand: 1. The directory entry it names shall be removed.  2. If the
 * dir operand includes more than one pathname component, effects
 * equivalent to the following command shall occur:
 * `rmdir -p $(dirname dir)`"
 *
 * That second step is recursive by construction (dirname of a multi-
 * component path is itself a path rmdir -p can be re-applied to), and
 * the standard does not say what happens when an ancestor's rmdir() in
 * that chain fails because it is not empty -- every real implementation
 * treats "an ancestor is not empty" as the expected, silent stopping
 * condition (of course /home is not empty just because `rmdir -p
 * /home/me/tmp` emptied /home/me) and reserves the diagnostic for any
 * *other* failure (permission denied, and so on), which is the
 * distinction implemented below.
 *
 * EXIT STATUS: "0 Each directory entry specified by a dir operand was
 * removed successfully." ">0 An error occurred." -- same diagnose-and-
 * continue loop shape as src/util/mkdir_util.c.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <unistd.h>
#include "util.h"

/* Ascends from `dir` (already removed by the caller) via dirname(),
 * rmdir()-ing each ancestor until one is not empty (quiet stop, not an
 * error), the root is reached (dirname() stops shortening -- also a
 * quiet stop), or a real error occurs (diagnosed, and reported to the
 * caller via a nonzero return so the overall exit status reflects it).
 */
static int rmdir_ascend(const char *dir)
{
	char buf[PATH_MAX];
	size_t n = strnlen(dir, sizeof buf);

	if (n >= sizeof buf) return 0; /* nothing sensible to ascend from */
	memcpy(buf, dir, n);
	buf[n] = 0;

	for (;;) {
		char prev[PATH_MAX];
		char *parent;
		size_t pn = strnlen(buf, sizeof buf);
		if (pn == sizeof buf) return 0;
		memcpy(prev, buf, pn + 1);

		parent = dirname(buf); /* mutates buf in place; parent aliases it */
		if (!strcmp(parent, ".") || !strcmp(parent, prev)) return 0;

		if (rmdir(parent) != 0) {
			if (errno == ENOTEMPTY) return 0;
			__util_diagf("rmdir: %s: %s\n", parent, strerror(errno));
			return -1;
		}
		/* buf == parent already (dirname() mutated it in place);
		 * loop around to strip the next component. */
	}
}

int __util_rmdir_main(int argc, char **argv)
{
	int i, opt_p = 0, fail = 0;

	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
		if (!strcmp(argv[i], "--")) { i++; break; }
		if (!strcmp(argv[i], "-p")) { opt_p = 1; continue; }
		__util_diagf("rmdir: %s: invalid option\n", argv[i]);
		return 1;
	}
	if (i >= argc) {
		__util_diagf("rmdir: missing operand\n");
		return 1;
	}

	for (; i < argc; i++) {
		if (rmdir(argv[i]) != 0) {
			__util_diagf("rmdir: %s: %s\n", argv[i], strerror(errno));
			fail = 1;
			continue;
		}
		if (opt_p && rmdir_ascend(argv[i]) < 0) fail = 1;
	}
	return fail;
}

// NOLINTEND(misc-include-cleaner)
