/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * printf -- the `printf` utility (XCU printf(1p); NOT the same thing as
 * this library's own C printf() in include/stdio.h -- see
 * src/util/util_printf.c's header for the distinction and for why that
 * file, not src/util/printf.c, is where __util_printf_main() lives).
 *
 * The whole of it is __util_printf_main() in src/util/util_printf.c,
 * compiled into libc.a and shared with src/sh/builtin.c's bi_printf();
 * this file is the thin main() over it, the same shape sh/main.c is
 * over __sh_main() (see that file's own comment and
 * src/internal/util.h's).
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_printf_main(argc, argv);
}
