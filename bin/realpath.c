/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * realpath -- not an XCU utility; see src/util/realpath.c's own comment
 * for why it lives in this tier anyway.
 *
 * The whole of it is __util_realpath_main() in src/util/realpath.c,
 * compiled into libc.a and shared with src/sh/builtin.c's
 * bi_realpath(); this file is the thin main() over it, the same shape
 * sh/main.c is over __sh_main() (see that file's own comment and
 * src/internal/util.h's).
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_realpath_main(argc, argv);
}
