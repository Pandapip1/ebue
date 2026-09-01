/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * timeout -- the `timeout` utility (not XCU-mandatory -- see
 * src/util/timeout.c's own header comment for the verification and
 * the GNU-heritage semantics this implements).
 *
 * The whole of it is __util_timeout_main() in src/util/timeout.c,
 * compiled into libc.a and shared with src/sh/builtin.c's bi_timeout();
 * this file is the thin main() over it, the same shape sh/main.c is
 * over __sh_main() (see that file's own comment and
 * src/internal/util.h's).
 */
#include "../src/internal/util.h"

int main(int argc, char **argv)
{
	return __util_timeout_main(argc, argv);
}
