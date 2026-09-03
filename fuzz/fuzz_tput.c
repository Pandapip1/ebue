/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_tput_main() -- src/util/tput.c's `tput [-T type] operand
 * [args...]` argument parser: the -T/-Ttype terminal-type-selection
 * forms, the built-in five-terminal table lookup (lookup_term()), and
 * the capability-name dispatch (clear/init/reset/cols/lines/bold/smso/
 * rmso/smul/rmul/rev/sgr0/cup) each operand name resolves to.
 *
 * Same NUL-delimited tokenized-argv shape as fuzz_expr.c: no separate
 * lexer exists to fuzz, because tput's grammar is entirely argv-shaped.
 * argv[0] is always the fixed string "tput"; every token after it,
 * fuzzer-controlled, becomes one argv element in order -- covering the
 * "-T type" two-token form, the "-Ttype" single-token compact form, a
 * bare "-T" with nothing after it (usage error), any capability-name
 * operand, and cup's row/col operand pair.
 *
 * Both stdout and stderr are redirected: every successful capability
 * lookup prints the real escape sequence to stdout (print_string()/
 * print_numeric()/print_cup()), and every parse or lookup failure prints
 * one line to stderr via __util_diagf().
 *
 * That redirection also sidesteps live_dimension()'s ioctl() call at
 * runtime: tput.c's cols/lines path tries isatty(1) before falling back
 * to term_table's static value, and freopen() above makes fd 1 a
 * regular file, so isatty(1) is false and ioctl(1, TIOCGWINSZ, ...) is
 * never reached -- which matters because src/ioctl/ioctl.c's
 * TIOCGWINSZ case needs __plat_tiocgwinsz, missing from this native
 * build's link (see fuzz_stty.c's header). This breaks the link of
 * every harness in fuzz/Makefile's HARNESSES list, not just this one,
 * since ioctl.o is linked unconditionally -- a pre-existing defect
 * outside this file's scope, not fixed by this file's own runtime
 * behavior never depending on the missing symbol.
 *
 * Checked: tput.c's EXIT STATUS section, mapped onto concrete return
 * values -- every `return` in the file is one of exactly 0, 1, 2, 3, 4
 * or 5, narrower than POSIX's own bare ">4 An error occurred" for the
 * failure tail. No exit()/_exit() call anywhere in the file either;
 * libFuzzer's own atexit-based detection is the backstop.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define CAP_TOKENS 6
#define CAP_SCRATCH 128
#define ROOT "/tmp/tputfz"

static int redirect_streams(void)
{
	if (!freopen(ROOT "/out", "w", stdout)) return 0;
	if (!freopen(ROOT "/err", "w", stderr)) return 0;
	return 1;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char scratch[CAP_SCRATCH];
	char argv0[] = "tput";
	char *argv[CAP_TOKENS + 2];
	int argc = 1;
	size_t si = 0, wi = 0;
	int rc;

	mkdir(ROOT, 0755);
	if (!redirect_streams()) return 0;

	argv[0] = argv0;

	while (si < size && argc < CAP_TOKENS + 1 && wi < CAP_SCRATCH - 1) {
		size_t start = wi;

		while (si < size && data[si] != 0 && wi < CAP_SCRATCH - 1)
			scratch[wi++] = (char)data[si++];
		scratch[wi++] = 0;
		argv[argc++] = &scratch[start];

		if (si < size && data[si] == 0) si++;   /* consume the delimiter itself */
	}
	argv[argc] = NULL;

	rc = __util_tput_main(argc, argv);
	fflush(stdout);
	fflush(stderr);
	if (rc < 0 || rc > 5)
		oracle_mismatch_i("tput returned an exit status outside {0..5}",
		                  argc > 1 ? argv[argc - 1] : "", rc, 0);

	return 0;
}
