/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * m4 -- the `m4` utility (XCU m4(1p)).
 *
 * The whole of it is __util_m4_main() in src/util/m4.c, compiled into
 * libc.a and shared with src/sh/builtin.c's bi_m4(); this file is the
 * thin main() over it, the same shape sh/main.c is over __sh_main()
 * (see that file's own comment and src/internal/util.h's).
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_m4_main(argc, argv);
}
