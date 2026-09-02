/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Named util_pwd.c, not pwd.c: same tcc-`ar` 15-byte member-name
 * collision with src/misc/pwd.c (the unrelated <pwd.h> getpwnam()/
 * getpwuid() implementation) that src/util/util_basename.c's header
 * explains in full -- the util_ prefix is the whole fix, the exported
 * symbol is still __util_pwd_main().
 *
 * pwd(1p).  SYNOPSIS: "pwd [-L|-P]".  OPERANDS: "None."  STDOUT: "an
 * absolute pathname of the current working directory: \"%s\\n\",
 * <directory pathname>".
 *
 * -L/-P: "If the PWD environment variable contains an absolute pathname
 * of the current directory and [it has no dot/dot-dot components], pwd
 * shall write this pathname... Otherwise, the -L option shall behave as
 * the -P option" / "-P: The pathname written to standard output shall
 * not contain any components that refer to files of type symbolic
 * link."  This mirrors src/sh/builtin.c's bi_cd(), whose own comment
 * says it is "deliberately not a complete cd(1p): no ... -L/-P
 * logical/physical distinction" -- the same simplification applies
 * here, for the same reason: this process has no shell-maintained $PWD
 * chain of its own to validate and fall back from, only the real
 * getcwd(), which is inherently the -P (symlink-free, physical) answer.
 * So -L and -P are both accepted and both do exactly what no option
 * does.  What is refused, loudly rather than silently, is anything
 * else: an unrecognized option, or any operand at all (pwd(1p)'s
 * OPERANDS section is "None") -- see test/sh-design.md's refusal list
 * and src/sh/builtin.c's bi_set() for why a silent no-op is worse than
 * a diagnostic here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "util.h"

int __util_pwd_main(
	int argc, char **argv elements_withtok(null_terminated, argc))
{
	int i;
	char *cwd;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-L") && strcmp(argv[i], "-P")) { // NOLINT(bugprone-suspicious-string-compare) -- nonzero from both calls intentionally means neither supported option matches
			__util_diagf("pwd: %s: unsupported option or operand -- "
			                "pwd(1p) takes no operands\n", argv[i]);
			return 2;
		}
	}

	/* getcwd(0, 0): "If buf is a null pointer, ... the space ... shall
	 * be allocated as necessary" -- src/sh/builtin.c's bi_cd() already
	 * relies on the same allocating form. */
	cwd = getcwd(0, 0);
	if (!cwd) {
		perror("pwd");
		return 1;
	}
	if (fputs(cwd, stdout) < 0 || fputc('\n', stdout) == EOF ||
	    fflush(stdout) != 0) {
		free(cwd);
		return 1;
	}
	free(cwd);
	return 0;
}
