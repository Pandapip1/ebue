/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_tput_main() -- src/util/tput.c's `tput [-T type] operand
 * [args...]` argument parser: the -T/-Ttype terminal-type-selection
 * forms, the built-in five-terminal table lookup (lookup_term()), and
 * the capability-name dispatch (clear/init/reset/cols/lines/bold/smso/
 * rmso/smul/rmul/rev/sgr0/cup) each operand name resolves to -- see that
 * file's own header comment for the full POSIX-vs-extension boundary and
 * the exact EXIT STATUS mapping this harness checks below.
 *
 * TURNING BYTES INTO ARGV.  Same NUL-delimited tokenized-argv shape
 * fuzz_expr.c's header comment describes: no separate lexer exists to
 * fuzz, because tput's grammar -- like expr's, test's, and find's -- is
 * entirely argv-shaped, and this harness's only job is to do the
 * word-splitting a real shell would have done. argv[0] is always the
 * fixed string "tput"; every token after it, fuzzer-controlled, becomes
 * one argv element in order -- covering the "-T type" two-token form,
 * the "-Ttype" single-token compact form (__util_tput_main()'s
 * `strncmp(argv[1], "-T", 2)` case), a bare "-T" with nothing after it
 * (usage error), any capability-name operand, and cup's row/col operand
 * pair, all without this harness needing to encode tput's grammar
 * itself.
 *
 * NO DICTIONARY, NO SEED CORPUS SHIPPED HERE EITHER, matching the rest
 * of this directory: tput's known vocabulary (five terminal-type names,
 * eleven capname strings) is a small fixed-string set pure random
 * mutation is slow to discover on a cold corpus, but coverage-guided
 * mutation plus tools/fuzz.sh's persistent corpus is this directory's
 * consistent answer rather than a hand-written dictionary.
 *
 * STDOUT/STDERR REDIRECTION: same freopen()-a-fixed-sink-file mechanism
 * as elsewhere in this directory -- every successful capability lookup
 * prints the real escape sequence to stdout (print_string()/
 * print_numeric()/print_cup()), and every parse or lookup failure prints
 * one line to stderr via __util_diagf().
 *
 * This redirection also sidesteps live_dimension()'s ioctl() call at
 * runtime: tput.c's cols/lines path tries isatty(1) before falling back
 * to term_table's static value, and freopen() above makes fd 1 a
 * regular file, so isatty(1) is false and ioctl(1, TIOCGWINSZ, ...) is
 * never reached. That matters because src/ioctl/ioctl.c's TIOCGWINSZ
 * case needs __plat_tiocgwinsz, which is missing from this native
 * build's link for the same reason fuzz_stty.c's header documents at
 * length (src/ioctl/linux/ is skipped by asan-build.sh's file-selection
 * loop even though this native build predefines __linux__) -- breaking
 * the link of every harness in fuzz/Makefile's HARNESSES list, not just
 * this one, since ioctl.o is linked unconditionally regardless of
 * whether a given harness calls ioctl(). Reported rather than worked
 * around: this file's own runtime behaviour never depends on the
 * missing symbol (isatty(1) is false first), but that does not fix the
 * link itself, which is a pre-existing defect in src/ioctl/ioctl.c
 * outside this file's scope.
 *
 * WHAT IS CHECKED. tput.c's own EXIT STATUS section, mapped onto this
 * implementation's concrete return values in that file's own header
 * comment: every `return` in the file is one of exactly 0, 1, 2, 3, 4 or
 * 5, narrower than POSIX's own bare ">4 An error occurred" for the
 * failure tail, so that exact range is what this harness checks. No
 * exit()/_exit() call anywhere in the file either; libFuzzer's own
 * atexit-based detection is the backstop, as in every other harness
 * here.
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
