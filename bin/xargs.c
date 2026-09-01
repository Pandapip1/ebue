/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * xargs -- the `xargs` utility (XCU xargs(1p)).
 *
 * The whole of it is __util_xargs_main() in src/util/xargs.c, compiled
 * into libc.a and shared with src/sh/builtin.c's bi_xargs(); this file
 * is the thin main() over it, the same shape sh/main.c is over
 * __sh_main() (see that file's own comment and src/internal/util.h's).
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_xargs_main(argc, argv);
}
