/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_stty_main() -- src/util/stty.c's `stty [-a|-g] | operand...`
 * argument parser: parse_operand()'s boolean-flag/`-flag` toggle table
 * (boolflags[]), the non-boolean groups (cs5..cs8, the delay-mode names,
 * ispeed/ospeed/min/time's value-taking pairs, the nine control-character
 * names via parse_ctrl_char()), the combination-mode keywords (raw,
 * -raw/cooked, nl/-nl, ek, sane), and parse_saved()'s own ':'-joined
 * hex-field encoding for the sole-operand "saved settings" form -- see
 * that file's own header comment for the full XCU stty.html grouping
 * this table follows and the documented, deliberate readings of its
 * underspecified corners.
 *
 * TURNING BYTES INTO ARGV. Same NUL-delimited tokenized-argv shape
 * fuzz_expr.c's own header comment describes: stty's grammar, like
 * expr's, test's and tput's, is entirely a flat operand list -- there is
 * no separate lexer to fuzz, only the word-splitting a real shell would
 * already have done. argv[0] is always the fixed string "stty" (unlike
 * test(1p), stty has no second registered name the way "[" is for test,
 * so there is no analogue of fuzz_test.c's OPTION BYTE needed here).
 * Every token after argv[0] becomes one argv element in order, and
 * two-argv-element operands (ispeed N, min N, intr C, ...) need no
 * special harness support: the tokenizer just places the name in one
 * token and the value in the next.
 *
 * Unlike fuzz_test.c, no byte is reserved to pick an invocation shape.
 * Bare `stty` (argc==1) falls out of an empty input; argv[1] exactly
 * "-a" or "-g" (report_mode()'s two special forms) and "-a"/"-g"
 * combined with other operands (the "may not be combined" rejection)
 * both fall out of ordinary tokenization -- no forced trailing token is
 * needed the way fuzz_test.c needs a forced "]". The one shape this
 * harness does not go out of its way to construct is a valid
 * parse_saved() ':'-joined 22-hex-field token (stty.c's header gives the
 * exact shape): synthesising a plausible struct termios snapshot inside
 * this harness would be unlike every other harness's rare-shape
 * handling in this directory, so reaching parse_saved()'s success path
 * is left to the fuzzer's corpus, same as elsewhere.
 *
 * SAFETY: real fd 0 is forced to /dev/null before __util_stty_main() is
 * ever called, and must stay that way. stty(1p) is defined entirely over
 * standard input, and __util_stty_main() hard-codes fd 0 in every
 * tcgetattr(0, ...)/tcsetattr(0, ...) call -- no argv operand names a
 * different fd. Both ntlibc termios backends gate on "is fd 0 actually a
 * terminal" and answer ENOTTY, untouched, otherwise (get_console()'s
 * `f->type != __FD_CONSOLE` on NT; a real non-tty ioctl(2) failing
 * ENOTTY straight from the kernel on Linux) -- but neither backend is
 * actually linked into this harness's build: src/termios/termios.c is
 * `#ifndef __linux__`-guarded and this native ASan/fuzz build defines
 * __linux__, while src/termios/linux/plat_termios.c (the one that would
 * supply the real symbols) is skipped by asan-build.sh's file-selection
 * loop like every other src/<module>/linux/ source, and termios has no
 * nt/ subdirectory to fall back to. So a call to tcgetattr()/tcsetattr()
 * from stty.o resolves to the host's own real, dynamically-linked glibc
 * instead (fuzz/Makefile's link is not -nostdlib), which does two things
 * neither ntlibc backend would ever do:
 *
 *   (a) It operates on this process's REAL kernel fd 0 directly (a raw
 *       ioctl(2), not the simulated NT volume fuzz/ntstubs.c stands in
 *       for elsewhere in this harness). If real fd 0 happens to be an
 *       actual terminal (e.g. a developer running `make -C fuzz run`
 *       interactively, with no stdin redirection of its own), a fuzzed
 *       `stty` invocation would mutate that terminal's real settings.
 *   (b) On success, it writes the host's own struct termios (glibc's
 *       NCCS==32 layout) through a pointer this file's `struct termios
 *       t;` declares using ntlibc's own, differently sized <termios.h>
 *       (NCCS==16) -- a real stack buffer overrun, the same class of bug
 *       fuzz/Makefile's STATRENAME comment documents for stat().
 *
 * The fix: this harness cannot pass __util_stty_main() a different fd
 * (it hard-codes 0), and ntlibc's own open()/dup2()/close() go through
 * fuzz/ntstubs.c's simulated NT volume rather than the real fd table, so
 * this file instead reaches the host's real syscall(2) directly the same
 * way fuzz/ntstubs.c itself does (see its own SYS_openat/SYS_close call
 * sites): once, before any fuzzed argv reaches __util_stty_main(), open
 * "/dev/null" and dup2() it onto real fd 0. The SYS_openat/SYS_dup2/
 * SYS_close numbers below are x86_64's, matching fuzz/ntstubs.c's own
 * SYS_openat=257 -- this whole directory only ever builds for that one
 * architecture.
 *
 * With real fd 0 forced to /dev/null, tcgetattr(0, ...)/tcsetattr(0, ...)
 * always fail ENOTTY without touching their output argument (true of a
 * real ioctl(2) failure generally, and exactly what
 * src/termios/linux/plat_termios.c's own tcgetattr() does), and this
 * holds regardless of which of the three possible backends -- host glibc
 * today, or either ntlibc backend if the link gap above is ever fixed --
 * ends up answering fd 0. Every `return 0` in __util_stty_main() is
 * reachable only right after a successful tcgetattr/tcsetattr, and it
 * has no `return 2` anywhere, so with fd 0 forced to /dev/null the only
 * value it can legitimately return is 1; the setup steps below bail out
 * to a plain `return 0` (never calling __util_stty_main()) if the
 * redirection itself could not be arranged, rather than risk a false
 * report.
 *
 * Separately, fuzz_tput.c's header documents that src/ioctl/ioctl.c
 * references __plat_tiocgwinsz, which nothing in this build provides,
 * breaking the link of every harness in fuzz/Makefile's HARNESSES list
 * regardless of whether that harness calls ioctl() itself (stty.c's own
 * code never does). Unrelated to the termios gap above and out of scope
 * here, but it means this harness cannot actually be linked in this
 * sandbox today either way.
 *
 * ORACLE: rc must be exactly 1 (see above; stty.html itself only
 * requires "0 or nonzero").
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);
extern long syscall(long number, ...);

#define CAP_TOKENS 16
#define CAP_SCRATCH 512
#define ROOT "/tmp/sttyfz"

/* x86_64 syscall numbers -- see this file's own header comment for why
 * only x86_64's are needed here (matching fuzz/ntstubs.c's own
 * SYS_openat=257, the same architecture this whole directory's harnesses
 * are built for). */
#define SYS_OPENAT 257
#define SYS_DUP2   33
#define SYS_CLOSE  3

/* Force the REAL, kernel-level fd 0 to /dev/null exactly once per
 * process -- see this file's own header comment (SAFETY section) for
 * why this is required before __util_stty_main() may ever be called,
 * and why it only ever needs to happen once (stty never itself opens,
 * closes or otherwise repoints fd 0; only its *mode* is queried/changed,
 * which does not disturb what fd 0 refers to). Returns 1 once real fd 0
 * is known to be /dev/null, 0 if this could not be arranged -- in which
 * case the caller must not call __util_stty_main() at all this run. */
static int force_stdin_devnull(void)
{
	static int state; /* 0 untried, 1 ok, -1 permanently failed */
	long devnull, dr;

	if (state) return state > 0;

	devnull = syscall(SYS_OPENAT, -100 /* AT_FDCWD */, "/dev/null", 0 /* O_RDONLY */, 0);
	if (devnull < 0) { state = -1; return 0; }
	dr = syscall(SYS_DUP2, devnull, 0);
	syscall(SYS_CLOSE, devnull);
	state = (dr == 0) ? 1 : -1;
	return state > 0;
}

static int redirect_streams(void)
{
	if (!freopen(ROOT "/out", "w", stdout)) return 0;
	if (!freopen(ROOT "/err", "w", stderr)) return 0;
	return 1;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char scratch[CAP_SCRATCH];
	char argv0[] = "stty";
	char *argv[CAP_TOKENS + 2];
	int argc = 1;
	size_t si = 0, wi = 0;
	int rc;

	/* Real host fd 0 must be a guaranteed non-terminal before anything
	 * below ever runs -- see this file's own header comment. Bail
	 * out, calling neither __util_stty_main() nor anything else, if
	 * that could not be arranged this run. */
	if (!force_stdin_devnull()) return 0;

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

	rc = __util_stty_main(argc, argv);
	fflush(stdout);
	fflush(stderr);
	/* See this file's own header comment: with real fd 0 forced to
	 * /dev/null, __util_stty_main() can only ever return 1 here. */
	if (rc != 1)
		oracle_mismatch_i("__util_stty_main returned something other than 1 "
		                  "with stdin forced to /dev/null",
		                  argc > 1 ? argv[argc - 1] : "", rc, 1);

	return 0;
}
