/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * crontab -- the `crontab` utility (XCU crontab(1p)).
 *
 * The whole of it is __util_crontab_main() in src/util/crontab.c,
 * compiled into libc.a and shared with src/sh/builtin.c's
 * bi_crontab(); this file is the thin main() over it, the same shape
 * sh/main.c is over __sh_main() (see that file's own comment and
 * src/internal/util.h's).
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_crontab_main(argc, argv);
}
