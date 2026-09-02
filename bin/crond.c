/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * crond -- the cron scheduling daemon behind crontab(1p). NOT a shell
 * builtin: see src/util/crond.c's own header for exactly why a
 * long-lived daemon is a deliberate exception to this project's usual
 * "every utility is both a builtin and a standalone executable" rule,
 * and src/sh/builtin.c for the resulting, deliberate absence of a
 * bi_crond() there.
 *
 * The whole of crond's logic is __util_crond_main() in
 * src/util/crond.c, compiled into libc.a; this file is the thin
 * main() over it, the same shape sh/main.c is over __sh_main() (see
 * that file's own comment and src/internal/util.h's) -- except this
 * is the ONLY caller __util_crond_main() has, since there is no
 * builtin twin.
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_crond_main(argc, argv);
}
