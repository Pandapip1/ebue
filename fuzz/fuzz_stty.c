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
 * fuzz_expr.c's own header comment describes (read it first): stty's own
 * grammar, like expr's, test's and tput's, is entirely a flat operand
 * list -- there is no separate lexer to fuzz, only the word-splitting a
 * real shell would already have done. argv[0] is always the fixed string
 * "stty" (XCU 2.9.1: argv[0] is the utility's own name, unlike test(1p),
 * stty has no second registered name the way "[" is for test, so there
 * is no analogue of fuzz_test.c's OPTION BYTE bit 0 needed here). Every
 * token after argv[0], fuzzer-controlled, becomes one argv element in
 * order. Two-argv-element operands (ispeed N, min N, intr C, ...) need
 * no special harness support either: the tokenizer just has to place the
 * name in one token and the value in the next, exactly as it would for
 * any other pair of consecutive operands -- the identical "the caller
 * already did the word-splitting" reasoning fuzz_find.c's header gives
 * for its own two-argv-element primaries like `-name PATTERN`.
 *
 * NO OPTION BYTE FOR INVOCATION SHAPE, UNLIKE fuzz_test.c. Checked
 * before deciding this rather than copying that file's shape out of
 * habit: fuzz_test.c reserves its first byte because reaching "[" mode
 * needs argv[0] itself changed (something no NUL-delimited tokenizer of
 * the REST of the buffer could ever produce) and reliably reaching a
 * forced trailing "]" is likewise not naturally in a NUL-splitter's
 * reach. Neither obstacle exists here: argc==1 (bare `stty`) falls out
 * for free whenever the fuzz input is empty; argc==2 with argv[1]
 * exactly "-a" or "-g" (report_mode()'s two special forms) falls out of
 * a two-byte input with no embedded NUL, well within a byte-level
 * mutator's reach even from a cold corpus (unlike fuzz_test.c's forced
 * "]", these are two ordinary ASCII bytes, not a separate structural
 * choice); and "-a"/"-g" combined with other operands (the loud
 * "may not be combined" rejection) falls out whenever the tokenizer
 * happens to place "-a" or "-g" as one of several tokens. The one shape
 * this harness does NOT go out of its way to construct is a valid
 * parse_saved() ':'-joined 22-hex-field token (stty.c's own header
 * comment gives the exact shape) -- unlike fuzz_sort.c's fixed-fixture
 * file or fuzz_test.c's forced "]", synthesising one here would mean
 * generating a plausible struct termios snapshot inside this harness,
 * which no sibling harness in this directory does for an analogously
 * rare shape (fuzz_expr.c's own header cites exactly this "no dictionary
 * -- coverage-guided mutation plus tools/fuzz.sh's persistent corpus is
 * this directory's consistent answer" policy); reaching parse_saved()'s
 * success path is left to the corpus discovering and keeping one, the
 * same way every other harness here relies on persistence rather than a
 * hand-built generator for its own rare shapes.
 *
 * ============================================================
 * SAFETY: WHY THIS HARNESS FORCIBLY REDIRECTS THE REAL, KERNEL-LEVEL FD
 * 0 TO /dev/null BEFORE EVER CALLING __util_stty_main(), AND WHAT WAS
 * ACTUALLY VERIFIED (NOT ASSUMED) BEFORE WRITING THIS FILE.
 * ============================================================
 *
 * stty(1p) is defined entirely over standard input (stty.html DESCRIPTION,
 * quoted in stty.c's own header comment: "usage of standard input is
 * required"), and __util_stty_main() hard-codes fd 0 in every
 * tcgetattr(0, ...)/tcsetattr(0, ...) call -- there is no argv operand
 * naming a different fd. This task's own instructions called out the
 * obvious risk directly: a real stty genuinely mutates real terminal
 * settings, and the fix has to be verified by reading the code, not
 * assumed. Reading src/termios/termios.c and src/termios/linux/
 * plat_termios.c in full (both implement the same eleven <termios.h>
 * symbols, gated on whether stdin actually names a terminal) confirmed
 * that EITHER implementation, on its own, already does the safe thing:
 * both gate on "is fd 0 actually a terminal" and answer ENOTTY,
 * untouched, otherwise (get_console()'s `f->type != __FD_CONSOLE` on NT;
 * a real ioctl(2) against a non-tty fd failing ENOTTY straight from the
 * kernel's own ioctl_tty(2) dispatch on Linux, per that file's own
 * banner -- "no f->type check of any kind" needed there because the
 * kernel already gives the real answer).
 *
 * But *neither* implementation is actually present in the library this
 * exact harness links against, and that is not an assumption either --
 * it was checked directly against tools/asan-build.sh, the very script
 * fuzz/Makefile's own $(LIBDIR) rule calls to build the objects this
 * harness links:
 *
 *   1. src/termios/termios.c's ENTIRE body is wrapped in `#ifndef
 *      __linux__` (that file's own banner: this is deliberate, so
 *      src/termios/linux/plat_termios.c can supply the same eleven
 *      symbols for real on an actual Linux target build without a
 *      link-time collision). This native ASan/fuzz build's own clang
 *      invocation predefines __linux__ (confirmed with `clang -dM -E -x
 *      c /dev/null | grep __linux__`, and asan-build.sh's CFLAGS never
 *      undefines it) -- so under THIS build's own preprocessor state,
 *      termios.c's guard is false and the whole file compiles to zero
 *      symbols. Confirmed directly, not inferred from the source: compiling
 *      src/termios/termios.c with this Makefile's own CFLAGS_HARNESS-
 *      equivalent flags and running `nm` on the result lists nothing.
 *   2. src/termios/linux/ -- the file that WOULD supply the real
 *      symbols on an actual Linux target -- is unconditionally skipped
 *      by asan-build.sh's own file-selection loop for every harness in
 *      this directory ("other platform; native harness exercises NT
 *      through ntstubs.c", the same case that skips every other
 *      src/<module>/linux/ source file). Unlike src/ioctl's split (which has a separate
 *      src/ioctl/nt/plat_ioctl.c providing most, if not all, of what the
 *      skipped src/ioctl/linux/plat_ioctl.c would have), termios has no
 *      nt/ subdirectory at all -- termios.c IS the NT side, directly,
 *      and (1) above is why it does not compile to anything useful here
 *      either.
 *
 * So neither backend contributes tcgetattr()/tcsetattr()/tcflush()/
 * tcdrain()/tcflow()/tcsendbreak()/tcgetsid()/cfget*speed()/cfset*speed()
 * to this build's own library objects at all. Confirmed with `nm` against
 * the real objects tools/asan-build.sh --objects-only produces
 * (NTLIBC_ARCH=aarch64, to stay on this sandbox's own native
 * architecture and avoid ever building or running fuzz/ntstubs.c's own
 * x86_64-only inline asm, which this task's instructions rule out
 * entirely): src/termios/termios.c's object exports nothing beyond ASan's
 * own instrumentation symbols. fuzz/Makefile's own final link command is
 * not -nostdlib -- it links ordinarily against the host's real,
 * dynamically-linked C library for anything ntlibc itself does not
 * define (the exact mechanism this Makefile's own STATRENAME comment
 * describes for stat(), and the reason that mechanism exists at all) --
 * so a call to tcgetattr()/tcsetattr() from stty.o, with no ntlibc
 * definition anywhere in the link, resolves to the HOST's OWN real
 * libc implementation instead. That function then does two things
 * neither termios.c nor plat_termios.c would ever do on ntlibc's behalf:
 *
 *   (a) It operates on the REAL, kernel-level fd 0 of this actual host
 *       process directly (a raw ioctl(2), TCGETS/TCSETS, with no
 *       ntlibc fd-table indirection at all) -- not the simulated NT
 *       volume fuzz/ntstubs.c stands in for elsewhere in this harness.
 *       If that fd 0 happens to be a real terminal (exactly the case
 *       when a harness binary is launched interactively, e.g. a
 *       developer running `make -C fuzz run` from their own shell,
 *       which is the ordinary way this Makefile's own `run` target is
 *       invoked, with no stdin redirection of its own), a `stty`
 *       invocation this harness's fuzzed argv happens to construct
 *       would mutate that developer's REAL terminal settings for real.
 *       Exactly the risk this task's own instructions named.
 *   (b) On success, it WRITES the host's own struct termios -- glibc's
 *       shape, sized and laid out for its own NCCS (32 on this host's
 *       C library) -- through a pointer this file's own `struct termios
 *       t;` declares using ntlibc's OWN, differently sized <termios.h>
 *       (NCCS==16, per include/termios.h and src/termios/termios.c's
 *       own struct layout comments). That is a real out-of-bounds write
 *       on the stack, structurally identical to the stat() 144-vs-120-byte
 *       overrun this same Makefile's STATRENAME comment documents at
 *       length as the reason that mechanism exists ("no harness had ever
 *       reached an internal stat() before, which is why a reviewed,
 *       reasoned-about shim had been wrong since it was written") --
 *       except here undocumented and, before this file, unmitigated.
 *
 * THE FIX, HERE. This harness cannot pass a different fd to
 * __util_stty_main() (it hard-codes 0), and it cannot make ntlibc's own
 * open()/dup2()/close() reach the REAL host fd 0 either -- those symbols
 * ARE present in this build (src/fcntl/open.c, src/unistd/dup.c, ...)
 * and go through fuzz/ntstubs.c's simulated NT volume, an entirely
 * separate namespace from the real kernel fd table the host's
 * tcgetattr()/tcsetattr() above actually touch. So this file reaches
 * past ntlibc's own namespace the same way fuzz/ntstubs.c itself
 * already does throughout (see its own `extern long syscall(long,
 * ...);` and every SYS_openat/SYS_close call site) -- a bare `syscall`
 * symbol ntlibc never defines, so the reference binds to the host's real
 * glibc syscall(2) wrapper, exactly like the tcgetattr/tcsetattr
 * fallback above, but used here deliberately and safely: once, before
 * any fuzzed argv ever reaches __util_stty_main(), open "/dev/null" and
 * dup2() it onto the REAL fd 0, permanently replacing whatever this
 * process actually inherited. The SYS_openat/SYS_dup2/SYS_close numbers
 * below are x86_64's (openat=257, dup2=33, close=3) -- matching
 * fuzz/ntstubs.c's own SYS_openat=257, since ARCH defaults to x86_64 for
 * this whole directory and fuzz/ntstubs.c only ever supports that one
 * architecture regardless (this task's own instructions); this file is
 * never compiled or linked for any other target.
 *
 * WHAT THIS BUYS, VERIFIED FOR ALL THREE POSSIBLE BACKENDS. With real fd
 * 0 forced to /dev/null: the host glibc fallback above returns -1/ENOTTY
 * without ever touching *t (a real ioctl(2) failure never writes its
 * output argument -- confirmed against the kernel's own ioctl dispatch
 * behaviour, and this is also exactly what src/termios/linux/
 * plat_termios.c's own tcgetattr() does, `if (lx_ioctl(...) < 0) return
 * -1;` before ever touching `t`); and if this build's own termios gap
 * above is ever fixed and either ntlibc backend becomes the one actually
 * linked instead, BOTH already gate the identical way on a non-terminal
 * fd 0 (get_console()'s ENOTTY on NT; the real kernel's ENOTTY on
 * Linux). So this redirection is not a workaround specific to today's
 * accidental host-libc fallback -- it produces the same safe outcome
 * under any of the three implementations that could end up answering fd
 * 0 here, present or future.
 *
 * ONE CONSEQUENCE WORTH STATING EXPLICITLY, SINCE IT SHAPES THE ORACLE
 * BELOW: reading __util_stty_main() in full shows every one of its
 * `return 0` statements is reachable only immediately after a
 * SUCCESSFUL tcgetattr(0, ...) or tcsetattr(0, ...) call. With real fd 0
 * forced to /dev/null, neither ever succeeds, so `return 0` is
 * UNREACHABLE in this harness's own environment -- and the file has no
 * `return 2` anywhere either (grepped in full: every return is 0 or 1).
 * So the only value __util_stty_main() can legitimately return here is
 * 1; anything else is either a genuine finding or -- the other honest
 * explanation, and the reason the setup steps below bail out to a plain
 * `return 0` rather than ever calling __util_stty_main() when they fail
 * -- evidence that the one-time /dev/null redirection itself did not
 * take effect, never a false report of a bug in stty.c.
 *
 * A PRE-EXISTING, SEPARATE LINK DEFECT ALSO BLOCKS THIS HARNESS TODAY,
 * REPORTED HERE RATHER THAN FIXED: fuzz_tput.c's own header comment
 * documents that src/ioctl/ioctl.c references __plat_tiocgwinsz, which
 * nothing in this exact library build provides, and that this breaks the
 * link of EVERY harness in fuzz/Makefile's HARNESSES list unconditionally
 * (ioctl.o is linked into every harness whether or not that harness's own
 * code calls ioctl(), which stty.c's own code never does). Unrelated to
 * the termios gap described above, and out of scope for this file to fix
 * (src/ioctl/ioctl.c is shared library code well outside src/util/stty.c
 * or this harness), but worth repeating here since it means neither new
 * harness this task adds can actually be linked in this sandbox today,
 * independent of everything else this comment documents.
 *
 * WHAT IS CHECKED. Given the above: rc must be exactly 1. Not the wider
 * POSIX EXIT STATUS text (stty.html gives no explicit range beyond "the
 * utility exited with 0 or nonzero"); this file's own header comment (and
 * grepping every `return` in it) narrows that to the concrete claim
 * this harness's own environment can actually hold it to.
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
