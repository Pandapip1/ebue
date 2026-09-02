/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_xargs_main() -- src/util/xargs.c's own "Guideline" tokenizer
 * (read_tokens(): bare text, "..." and '...' as three ways to build up
 * one token, backslash-escaping outside any quote, per-token line
 * tracking for -I/-L) and the batching logic built on top of it
 * (byte-size and token-count batching against sysconf(_SC_ARG_MAX)/
 * sysconf(_SC_LINE_MAX), -I's per-line substitution via subst(), -L's
 * whole-line grouping).
 *
 * WHAT IS FUZZED, AND HOW.  Unlike fuzz_expr.c's and fuzz_find.c's
 * argv-shaped harnesses, xargs' own quoting/escaping grammar operates on
 * a byte STREAM, not on already-split argv words -- read_tokens() (this
 * file's target) calls fgetc(stdin) directly, one byte at a time, per
 * its own comment "unlike shell quoting". This harness therefore does
 * the opposite of fuzz_expr.c: it leaves argv almost entirely FIXED (see
 * SAFETY EXCLUSION below for why) and instead redirects the real process
 * stdin onto a file holding the fuzz bytes verbatim, exactly the
 * freopen() technique fuzz_ed.c's own header comment explains at length
 * (`stdin` is `FILE *const` in this libc's <stdio.h>, so freopen() --
 * which reuses the existing FILE* object -- is the only way to point it
 * somewhere other than the real inherited stream; fopen() cannot).
 *
 * Byte 0 selects among -t/-x/-p/-I/-L/-n/-s (see OPTION BYTE below); the
 * rest, capped at STDIN_CAP, becomes the whole of stdin, unfiltered --
 * embedded NUL bytes included, unlike every argv-token harness in this
 * directory, because read_tokens()'s fgetc() loop has no notion of a
 * C-string terminator at all; a NUL byte reaching it is just another
 * byte of "bare text" per the grammar's own EXTENDED DESCRIPTION, and
 * excluding it here would be excluding real, legal input from coverage
 * for no reason.
 *
 * A SAFETY EXCLUSION, NOT A COVERAGE TRADEOFF: which program is run.
 * Read fuzz_ed.c's own header comment on `!` in full before this
 * paragraph -- __util_xargs_main() reaching run_one() -> spawn_and_wait()
 * -> __find_program()/__spawn() is find(1p)'s -exec/-ok risk restated for
 * this utility's *entire* reason for existing (there is no xargs
 * invocation that does not eventually try to run something), which makes
 * "exclude the primary that reaches it" (fuzz_find.c's own fix) not
 * available here -- some invocation always happens. What stays entirely
 * out of the fuzzer's control instead is WHICH program: PROG below is a
 * fixed, compile-time constant naming a file this harness never creates,
 * under a directory component ('/'), so src/process/find_program.c's own
 * has_dir() takes it "as-is" rather than searching $PATH for it (see that
 * file's own header comment) -- no environment property (an empty PATH,
 * an absent shell) is being trusted to keep failing forever the way
 * fuzz_ed.c's own header comment says not to trust one; the guarantee
 * here is structural instead: __spawn() -> NT process creation ->
 * ultimately execve() of a path that provably does not exist can only
 * ever fail with ENOENT, the same universally-safe "no such file" failure
 * any execve() of a nonexistent path produces on any Unix, never
 * "successfully launch a real interpreter and hand it fuzzer-chosen
 * text" the way `!`'s risk was. The fuzzer never sees PROG at all -- it
 * is not derived from, or influenced by, the fuzz buffer in any way -- so
 * there is no path by which a fuzzer input could redirect this harness at
 * a different, real executable.
 *
 * -p's PROMPT CONFIRMATION cannot hang this harness even when selected:
 * prompt_confirm() also reads from `stdin` with getchar(), but only after
 * read_tokens() has already consumed the entire redirected file up to
 * EOF (read_tokens() runs first, unconditionally, before any batch or any
 * run_one() call) -- so every getchar() prompt_confirm() issues sees EOF
 * immediately and returns "not confirmed" without blocking, the same
 * fact that lets -p be included in the option byte at all rather than
 * excluded the way -exec/-ok are in fuzz_find.c.
 *
 * REDIRECTED: stdout/stderr, for the identical reason and by the
 * identical mechanism as fuzz_sed.c's and fuzz_ed.c's own header
 * comments give -- -t/-p's trace_line()/prompt_confirm() write to
 * stderr, and __util_diagf() (src/internal/util.h) always does too, on
 * every one of millions of calls.
 *
 * WHAT IS CHECKED.  xargs(1p)'s own EXIT STATUS section, cited in full in
 * src/util/xargs.c's header comment: "0 ... 1-125 ... 126 ... found but
 * could not be invoked ... 127 ... could not be found." Every code this
 * harness can actually observe stays inside [0, 127] by that contract
 * (PROG can only ever yield 126 or 127, never a genuine utility exit
 * code, since it never really runs -- see SAFETY EXCLUSION above), so a
 * value outside that range is asserted as a defect.
 *
 * SIZE CAPS.  512 bytes of stdin content -- generous next to
 * fuzz_sed.c's 480-byte script cap for a structurally similar streaming
 * grammar, small enough that read_tokens()'s own unbounded-growth
 * buf_putc()/emit_token() realloc loop stays cheap per call.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define STDIN_CAP 512
#define ROOT "/tmp/xargsfz"

/* Never created anywhere in this file, deliberately -- see this file's
 * header comment on SAFETY EXCLUSION.  The directory component is what
 * makes src/process/find_program.c's has_dir() take it as-is instead of
 * consulting $PATH. */
#define PROG ROOT "/__NEVER_CREATE_THIS_FILE__"

static void write_file(const char *path, const char *data, size_t len)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return;
	(void)write(fd, data, len);
	close(fd);
}

static void fixture(void)
{
	static int done;
	if (done) return;
	done = 1;
	mkdir(ROOT, 0755);
}

static int redirect_streams(void)
{
	if (!freopen(ROOT "/stdin", "r", stdin)) return 0;
	if (!freopen(ROOT "/out", "w", stdout)) return 0;
	if (!freopen(ROOT "/err", "w", stderr)) return 0;
	return 1;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	unsigned opt;
	size_t n;
	char content[STDIN_CAP];
	char argv0[] = "xargs";
	char a_t[] = "-t", a_x[] = "-x", a_p[] = "-p";
	char a_I[] = "-I", a_tmpl[] = "{}";
	char a_L[] = "-L", a_n[] = "-n", a_s[] = "-s";
	char numbuf[16];
	char *argv[12];
	int argc = 0;
	int rc;

	if (size < 1) return 0;
	fixture();

	opt = data[0];
	data++; size--;

	n = size < STDIN_CAP ? size : STDIN_CAP;
	memcpy(content, data, n);
	write_file(ROOT "/stdin", content, n);

	if (!redirect_streams()) return 0;

	argv[argc++] = argv0;
	if (opt & 0x01) argv[argc++] = a_t;
	if (opt & 0x02) argv[argc++] = a_x;
	if (opt & 0x04) argv[argc++] = a_p;

	/* -I and -L/-n/-s are mutually exclusive in different ways (see
	 * src/util/xargs.c's own header comment); letting the fuzzer pick
	 * ONE numeric batching mode via a two-bit field, rather than
	 * independent bits for -I/-L/-n/-s, exercises each mode's own
	 * batching logic without spending most inputs on the "-I and -L
	 * are mutually exclusive" early-exit path. */
	switch ((opt >> 3) & 0x03) {
	case 1:
		argv[argc++] = a_I;
		argv[argc++] = a_tmpl;
		break;
	case 2:
		argv[argc++] = a_L;
		snprintf(numbuf, sizeof numbuf, "%u", 1u + (opt >> 5));
		argv[argc++] = numbuf;
		break;
	case 3:
		argv[argc++] = (opt & 0x80) ? a_s : a_n;
		snprintf(numbuf, sizeof numbuf, "%u", 1u + (opt >> 5));
		argv[argc++] = numbuf;
		break;
	default:
		break;
	}

	argv[argc++] = (char *)PROG;
	if (((opt >> 3) & 0x03) == 1) argv[argc++] = a_tmpl;   /* give -I's template something to substitute */
	argv[argc] = NULL;

	rc = __util_xargs_main(argc, argv);
	fflush(stdout);
	fflush(stderr);
	if (rc < 0 || rc > 127)
		oracle_mismatch_i("xargs returned an exit status outside [0,127]", "", rc, 0);

	return 0;
}
