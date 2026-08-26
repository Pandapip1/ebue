/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * sh -- the `sh` utility (XCU sh(1p)).
 *
 * The whole of it is __sh_main() in src/sh/script.c, compiled into
 * libc.a with the rest of the engine; this file is the "thin main()
 * over them" the shell's design note asks the binary to be, and nothing
 * else.  It was not always this thin: the utility's option handling,
 * its refusal preflight and its run used to live here, which put them
 * out of reach of the one other caller that needs exactly them -- the
 * [ENOEXEC] interpreter of XSH exec and XCU 2.9.1 (see
 * src/process/exec.c).  That caller reached them by spawning this
 * binary instead, which meant finding it; the fix was to move the code
 * under src/ rather than to keep improving the search.
 *
 * This file still exists, and still builds its own binary, for the
 * reason CONTRIBUTING.md's "Why a shell lives in a libc repo" gives: sh
 * is a separate deliverable of this repo, with its own source directory
 * and its own name, rather than something blurred into src/.  The
 * Makefile's wildcard over src/ is also why the body of the utility
 * cannot be called `main` over there.
 */
#include "../src/sh/sh.h"

int main(int argc, char **argv)
{
	return __sh_main(argc, argv);
}
