/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * batch -- the `batch` utility (XCU batch(1p)).
 *
 * The whole of it is __util_batch_main() in src/util/batch.c, compiled
 * into libc.a and shared with src/sh/builtin.c's bi_batch(); this file
 * is the thin main() over it, the same shape sh/main.c is over
 * __sh_main() (see that file's own comment and src/internal/util.h's).
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_batch_main(argc, argv);
}
