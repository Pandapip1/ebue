/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * atd -- the at(1p)/batch(1p) scheduling daemon. NOT a shell builtin:
 * see src/util/atd.c's own header for exactly why a long-lived daemon
 * is a deliberate exception to this project's usual "every utility is
 * both a builtin and a standalone executable" rule, and
 * src/sh/builtin.c for the resulting, deliberate absence of a
 * bi_atd() there.
 *
 * The whole of atd's logic is __util_atd_main() in src/util/atd.c,
 * compiled into libc.a; this file is the thin main() over it, the
 * same shape sh/main.c is over __sh_main() (see that file's own
 * comment and src/internal/util.h's) -- except this is the ONLY
 * caller __util_atd_main() has, since there is no builtin twin.
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_atd_main(argc, argv);
}
