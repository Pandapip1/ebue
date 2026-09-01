/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * file -- the `file` utility (XCU file(1p)).
 *
 * The whole of it is __util_file_main() in src/util/util_file.c (not
 * src/util/file.c -- see that file's own header comment and
 * src/internal/util.h's for the ar-member-name collision with this
 * library's own src/stdio/file.c this dodges), compiled into libc.a
 * and shared with src/sh/builtin.c's bi_file(); this file is the thin
 * main() over it, the same shape sh/main.c is over __sh_main() (see
 * that file's own comment and src/internal/util.h's).  The standalone
 * executable this produces is still named file.exe -- only the
 * library source file's own basename changed, nothing about the
 * utility's own name or argv[0].
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_file_main(argc, argv);
}
