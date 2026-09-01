/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * sed -- the `sed` utility (XCU sed(1p)).
 *
 * The whole of it is __util_sed_main() in src/util/sed.c, compiled
 * into libc.a and shared with src/sh/builtin.c's bi_sed(); this file
 * is the thin main() over it, the same shape sh/main.c is over
 * __sh_main() (see that file's own comment and src/internal/util.h's).
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_sed_main(argc, argv);
}
