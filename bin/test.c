/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * test -- the `test` utility (XCU test(1p)).
 *
 * The whole expression engine is __util_test_main() in src/util/test.c,
 * compiled into libc.a and shared with src/sh/builtin.c's bi_test();
 * this file is the thin main() over it, the same shape sh/main.c is
 * over __sh_main() (see that file's own comment and
 * src/internal/util.h's).
 *
 * There is deliberately no standalone `[.exe` built alongside this one:
 * `[` as a filename does not survive $(wildcard)'s glob(3) semantics
 * cleanly (it opens a bracket-expression), and the `[` spelling is
 * already reachable through the shell built-in (src/sh/builtin.c
 * registers both "test" and "[" against the same bi_test(), which calls
 * this same __util_test_main() and does the same argv[0]-based
 * closing-bracket check).  A standalone `[.exe` is a real, tracked gap,
 * not an oversight -- revisit if something needs `[` specifically off
 * PATH rather than as a builtin.
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_test_main(argc, argv);
}
