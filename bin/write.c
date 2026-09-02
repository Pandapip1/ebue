/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * write -- the `write` utility (XCU write(1p)).
 *
 * The whole of it is __util_write_main() in src/util/util_write.c (the
 * util_ prefix avoids an ar member-name collision with src/unistd/
 * write.c -- see that file's own header comment), compiled into
 * libc.a and shared with src/sh/builtin.c's bi_write(); this file is
 * the thin main() over it, the same shape sh/main.c is over
 * __sh_main() (see that file's own comment and src/internal/util.h's).
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_write_main(argc, argv);
}
