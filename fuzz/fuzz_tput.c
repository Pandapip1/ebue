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
 * fuzz_expr.c's own header comment describes (read it first): no
 * separate lexer exists to fuzz, because tput's own grammar -- like
 * expr's, test's, and find's -- is entirely argv-shaped, and this
 * harness's only job is to do the word-splitting a real shell would have
 * done. argv[0] is always the fixed string "tput" (XCU 2.9.1: argv[0] is
 * the utility's own name, never fuzzed input); every token after it,
 * fuzzer-controlled, becomes one argv element in order -- covering the
 * "-T type" two-token form, the "-Ttype" single-token compact form
 * (__util_tput_main()'s own `strncmp(argv[1], "-T", 2)` case), a bare
 * "-T" with nothing after it (usage error), any capability-name operand,
 * and cup's row/col operand pair, all without this harness needing to
 * encode tput's grammar itself -- exactly the same "the caller already
 * did the word-splitting" reasoning fuzz_expr.c's, fuzz_test.c's and
 * fuzz_find.c's own header comments give for their own targets.
 *
 * NO DICTIONARY, NO SEED CORPUS SHIPPED HERE EITHER -- matching
 * fuzz_expr.c's own header comment's stated project-wide policy (checked
 * again while writing this file: a directory listing of fuzz/ for any
 * *.dict file still finds nothing).
 * tput's own known vocabulary (five terminal-type names, eleven capname
 * strings) is exactly the kind of small fixed-string set pure random
 * mutation is slow to discover on a cold corpus, same as expr's operator
 * tokens -- but this directory's answer to that, consistently, is
 * coverage-guided mutation plus the persistent corpus tools/fuzz.sh
 * accumulates across repeated runs, not a hand-written dictionary or
 * seed file for one harness that every other one in this list does
 * without.
 *
 * STDOUT/STDERR REDIRECTION: same freopen()-a-fixed-sink-file mechanism
 * fuzz_sort.c's, fuzz_ar.c's and fuzz_pax.c's own header comments give,
 * and for the identical reason -- every successful capability lookup
 * here prints the real escape sequence to stdout (print_string()/
 * print_numeric()/print_cup()), and every parse or lookup failure prints
 * one line to stderr via __util_diagf().
 *
 * WHY THIS ALSO SIDESTEPS live_dimension()'s ioctl() CALL AT RUNTIME,
 * AND A REAL PRE-EXISTING LINK DEFECT THIS TASK FOUND WHILE READING THE
 * BUILD, NOT GUESSED AT: tput.c's cols/lines path tries isatty(1) before
 * falling back to term_table's static value (see that file's own header
 * comment, "cols/lines still try one real, live answer first"). The
 * freopen() above makes fd 1 a regular file under ntlibc's own fd table,
 * so isatty(1) is false and ioctl(1, TIOCGWINSZ, ...) is never reached
 * at *runtime* by this harness -- which turns out to matter for a
 * reason well beyond this file: reading fuzz/Makefile's own library
 * selection (tools/asan-build.sh) in full for this task found that
 * src/ioctl/ioctl.c's TIOCGWINSZ case is `#ifdef __linux__ ->
 * __plat_tiocgwinsz()` (src/ioctl/linux/plat_ioctl.c), and
 * src/ioctl/linux/ is one of the directories asan-build.sh's own
 * file-selection loop unconditionally skips for every harness in this
 * list ("other platform; native harness exercises NT through
 * ntstubs.c") -- while src/ioctl/nt/plat_ioctl.c, the file compiled in
 * its place, never defines __plat_tiocgwinsz at all (that symbol's own
 * declaration, src/internal/plat_ioctl.h, says why: "reachable only from
 * code already behind `#ifdef __linux__`, so an NT build never
 * references, and therefore never needs to link, [it]"). That premise
 * does not hold for THIS native build: clang here predefines __linux__
 * (confirmed with `clang -dM -E -x c /dev/null`), and asan-build.sh's
 * own CFLAGS never undefines it -- unlike the four files
 * -D_NTLIBC_NATIVE_BUILD exists specifically to tell apart from a real
 * Linux target build (see that flag's own comment in asan-build.sh),
 * which src/ioctl/ioctl.c is simply not among. Verified, not assumed:
 * building the real library objects this exact Makefile's $(LIBDIR)
 * rule builds (`tools/asan-build.sh --objects-only`, NTLIBC_ARCH=aarch64
 * to stay on this sandbox's own native architecture rather than the
 * x86_64 default) and linking them shows `__plat_tiocgwinsz` undefined,
 * with nothing else in the link providing it -- so the link of EVERY
 * harness in fuzz/Makefile's HARNESSES list, not just this one, fails
 * today, independent of anything in this file: ioctl.o, like every
 * other object under $(LIBDIR)/obj, is linked unconditionally whether
 * or not a given harness's own code ever calls ioctl(). Reported rather than
 * silently worked around: this file's own stdout redirection still
 * means tput's *runtime behaviour*, once that link defect is ever
 * fixed, never actually depends on the missing symbol here (isatty(1)
 * is false first), but that cannot and does not fix the link itself,
 * which is a pre-existing defect in shared library plumbing
 * (src/ioctl/ioctl.c), not something either new file this task adds
 * caused or can fix from inside fuzz/fuzz_tput.c or fuzz/fuzz_stty.c.
 *
 * WHAT IS CHECKED. tput.c's own EXIT STATUS section, quoted and mapped
 * onto this implementation's concrete return values in that file's own
 * header comment, and every `return` in the file (read in full) is one
 * of exactly 0, 1, 2, 3, 4 or 5 -- so that is the real, exact range this
 * harness checks, narrower than POSIX's own bare ">4 An error occurred"
 * for the failure tail, the same "the file's own documented contract,
 * not just what this harness's argv happens to reach" reasoning
 * fuzz_sort.c's and fuzz_ar.c's own headers give for their targets. No
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
