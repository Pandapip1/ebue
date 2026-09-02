/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * man -- the `man` utility.
 *
 * The whole of it is __util_man_main() in src/util/man.c, compiled
 * into libc.a and shared with src/sh/builtin.c's bi_man(); this file is
 * the thin main() over it, the same shape sh/main.c is over __sh_main()
 * (see that file's own comment and src/internal/util.h's).
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_man_main(argc, argv);
}
