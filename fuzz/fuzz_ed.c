/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_ed_main() -- src/util/ed.c, ed(1p)'s hand-written command
 * parser (line/'$'/mark/BRE addresses with '+'/'-' offsets, ranges,
 * s///, a/i/c text input, g/v/G/V, m/t/j/k/u/=/l/p/n/H/h/P/f/r/w/e/E/
 * q/Q) sharing src/regex/regex.c's regcomp()/regexec() with everything
 * else in this tree.
 *
 * WHAT IS FUZZED, AND HOW.  ed(1p) has no scripted-file operand for its
 * command language at all -- unlike sed's `-f`, __util_ed_main()'s own
 * top-level loop and its G/V interactive step both call
 * read_line_stdin(stdin) against the literal `stdin` global, never
 * against a caller-supplied FILE*, so the fuzz buffer becomes ed's
 * *command stream* by redirecting the real process stdin, once per
 * call, with freopen() -- exactly the technique this project's own task
 * description for this file anticipated ("freopen() reuses the
 * existing FILE* object rather than replacing the pointer") and the
 * reason `include/stdio.h` declares `stdin`/`stdout`/`stderr` as `FILE
 * *const`.  The one operand ed(1p) DOES take -- `[file]`, the buffer's
 * initial content -- is a small fixed six-line fixture, not derived
 * from the fuzz input, for the identical reason fuzz_sed.c's own header
 * comment gives for its `data` file: the grammar under test is the
 * command stream's, not the edited text's, and fuzz_regex.c already
 * owns BRE-against-arbitrary-text coverage.
 *
 * Byte 0 of the input selects `-s` (bit 0) and `-p '*'` (bit 1); the
 * rest, capped at CMD_CAP, is the command stream.
 *
 * A SAFETY EXCLUSION, NOT A COVERAGE TRADEOFF: `!`.  ed(1p)'s `!`
 * command, and the "!command" form e/r/w accept in place of a filename,
 * both reach cmd_bang()/popen() -> system()/popen() -> (src/process/
 * spawn.c's) __spawn() -> NT process creation.  fuzz/ntstubs.c's own
 * RtlCreateUserProcess is not a stub for that call: it really does
 * fork(2) and execve() a real host process (see its own long header
 * comment, and `syscall(SYS_execve, host, argv, ...)` in the body) --
 * confirmed by reading the file, not assumed.  A fuzzer that found a
 * `!`-prefixed line would therefore be a fuzzer that found arbitrary
 * host command execution, unsupervised, for as long as this harness
 * keeps running.  That is exactly the class of risk fuzz/Makefile's own
 * banner comment states in capitals for a different subsystem --
 * "NOTHING IN THIS DIRECTORY MAY INVOKE WINE" -- and the fix here is
 * the same shape: never let the fuzzer reach the call at all, rather
 * than trust ntlibc's simulated environment (empty PATH/ComSpec; see
 * ed_maybe_dangerous() below) to keep failing to resolve a shell
 * forever.  ed_maybe_dangerous() rejects the WHOLE input if a literal
 * '!' byte appears anywhere in it: every one of the four affected
 * forms -- the bare `!` command, and `e`/`E`/`r`/`w` with a leading `!`
 * on their filename argument -- requires that exact byte, so one
 * coarse byte-level check closes off the whole class before any
 * parsing happens, at the cost of the (comparatively small) coverage
 * of cmd_bang()/expand_percent()/the `is_bang` branches inside
 * cmd_edit()/cmd_read()/cmd_write().  That cost is accepted
 * deliberately and is not a close call.
 *
 * WHAT IS DELIBERATELY NOT FUZZED, beyond `!` and the data file:
 *
 *   - SIGINT/SIGHUP delivery.  __util_ed_main() installs real
 *     sigaction() handlers and polls `ed_interrupted`/`ed_hup` between
 *     logical steps (this file's own header comment on "THE
 *     BUILTIN-SAFETY SIGNAL DISCIPLINE"), and that install/restore
 *     dance runs on every call this harness makes -- but nothing here
 *     ever raises either signal, so ed_check_interrupt()'s "yes,
 *     interrupted" arm and the whole SIGHUP-save-and-quit path in
 *     __util_ed_main()'s own loop are never taken.  A harness that sent
 *     itself real signals mid-call, from a second thread or an
 *     alarm(), could close this gap; not attempted here, since ed(1p)'s
 *     own header comment already documents this exact polling
 *     discipline (matching src/util/dd.c's SIGINT precedent) as
 *     independently reasoned about and unlikely to hide a parser bug
 *     specifically.
 *
 * BOUNDING RUNAWAY COMPUTATION: NOT NEEDED HERE, AND WHY.  This file's
 * own task description raised the same concern it raised for sed's b/t
 * -- "ed's g/v global-command application ... a real loop construct" --
 * and the check it asked to make first is whether that concern is real
 * for THIS grammar, not just assumed by analogy.  Read in full,
 * src/util/ed.c's g/v/G/V implementation (the `case 'g': case 'v':
 * case 'G': case 'V':` arm) has no way to not terminate:
 *
 *   - ed's command language has no branch or label construct at all --
 *     no b/t, nothing resembling one.  g/v's own sub-command-list
 *     (non-interactive form) is a fixed, already-parsed array (`list`)
 *     walked once per matched line by a plain for loop bounded by
 *     `nmatched`, itself bounded by `ed->nlines`; nesting a second g/v
 *     inside that list is refused outright (`ctx == CTX_GLIST` at the
 *     top of the same switch arm), so there is no recursive
 *     amplification either.
 *   - `ed->nlines` -- the bound on every one of these loops, and on
 *     search_forward()/search_backward()'s own wraparound scan -- can
 *     only grow via a/i/c/r reading from the SAME finite command
 *     stream this harness controls (CMD_CAP-capped below), so it is
 *     itself bounded by the size of one fuzz input.
 *   - the interactive G/V step reads its next per-match command with
 *     `read_line_stdin(stdin)` -- the real process stdin, which this
 *     harness has freopen()ed onto a finite regular file -- and its own
 *     "real EOF: stop this G/V loop quietly" comment is exactly the
 *     behaviour observed: EOF ends the loop immediately rather than
 *     blocking, because a regular file, unlike a terminal or a pipe
 *     with a live writer, never blocks a read at EOF.
 *   - s///'s own match-scan loop (substitute_line()) advances
 *     search_pos by at least 1 every iteration and stops at
 *     `search_pos > len`, and regexec() itself is bounded the same way
 *     fuzz_regex.c's own long banner on MAX_STEPS describes.
 *
 * So every loop in this file's target is bounded by a quantity this
 * harness itself already caps, and none of them can spin without
 * consuming either buffer lines or stdin bytes this harness controls
 * the supply of.  No safe_to_exec()/sed_may_loop_forever()-style filter
 * is added here for exactly that reason -- adding one anyway would be
 * guarding against a hazard a full read of the target's control flow
 * does not show exists, which is the opposite mistake from skipping
 * the check fuzz_sed.c's own banner makes for sed's b/t.  CMD_CAP still
 * caps the command stream's byte length, which is what keeps the
 * bounded-but-real cost of a worst-case regexec() times worst-case
 * `nlines` small in absolute terms, the same reasoning fuzz_sed.c gives
 * for its own SCRIPT_CAP.
 *
 * STDOUT/STDERR REDIRECTION: see fuzz_sed.c's own header comment for
 * the freopen()-not-fopen() reasoning; the same applies here verbatim
 * (ed's p/n/l/=/f, every `?` and file-error diagnostic, and the
 * default null-command print all write to the real process stdout).
 *
 * NO ORACLE.  Same reasoning as fuzz_sed.c: no reference ed(1p) this
 * project could differentially compare against without every one of
 * this file's own documented SCOPE NARROWINGS (no `W`, no `#`, no `%`
 * address, single-level undo, ...) reading as a mismatch.  What is
 * checked is the same contract fuzz_sed.c checks:
 *
 *   - src/internal/util.h's banner: a real process exit status, never
 *     a raw errno or a boolean.  Unlike sed (0 or 1 on every path),
 *     ed(1p)'s own argv-parsing usage errors return 2 (`-p` with no
 *     argument, an unrecognised option, an extra operand) -- this
 *     harness's own argv is always well-formed, so those three arms
 *     are not expected to fire, but the assertion checks the real
 *     contract (0, 1, or 2), not the narrower range this harness
 *     happens to exercise;
 *   - the same banner's other half, and this file's own header
 *     comment's "THE BUILTIN-SAFETY SIGNAL DISCIPLINE" section, both
 *     independently state __util_ed_main() never calls exit()/_exit()
 *     -- bi_ed() (src/sh/builtin.c) runs it in-process, no fork, the
 *     same as bi_sed().  Relied on here exactly the way fuzz_sed.c
 *     relies on it: libFuzzer's own atexit-based defence against a
 *     target calling exit() mid-run is the thing that would surface a
 *     violation, not a bespoke check in this file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define ROOT "/tmp/edfz"
#define CMD_CAP 480

/* ==== fixture: a small, fixed data file ed opens as its initial buffer.
 * Built once; see this file's header comment for why its content is NOT
 * derived from the fuzz input. ============================================== */

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
	static const char data[] =
		"hello world\n"
		"foo123bar\n"
		"\ttabbed\tfield\n"
		"special & chars \\ here\n"
		"\n"
		"the quick brown fox jumps\n";

	if (done) return;
	done = 1;
	mkdir(ROOT, 0755);
	write_file(ROOT "/data", data, sizeof data - 1);
}

/* ==== ed_maybe_dangerous(): the '!' safety exclusion -- see this
 * file's header comment.  Unlike fuzz_sed.c's sed_may_loop_forever(),
 * this is not an approximation of anything: EVERY path that can reach
 * a real subprocess requires this exact byte, so a plain memchr() is
 * already exact, not merely conservative. ==================================== */

static int ed_maybe_dangerous(const char *s, size_t n)
{
	return memchr(s, '!', n) != 0;
}

/* ==== stdin/stdout/stderr redirection -- see this file's header
 * comment and fuzz_sed.c's for the freopen()-not-fopen() reasoning. ======== */

static int redirect_streams(void)
{
	if (!freopen(ROOT "/cmds", "r", stdin)) return 0;
	if (!freopen(ROOT "/out", "w", stdout)) return 0;
	if (!freopen(ROOT "/err", "w", stderr)) return 0;
	return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char cmds[CMD_CAP + 1];
	size_t n;
	int opt_s, opt_p;
	int status;
	char *argv[6];
	int argc = 0;

	if (size < 1) return 0;
	fixture();

	opt_s = data[0] & 1;
	opt_p = (data[0] >> 1) & 1;
	data++; size--;

	n = size < CMD_CAP ? size : CMD_CAP;
	memcpy(cmds, data, n);
	cmds[n] = 0;
	if (memchr(cmds, 0, n)) return 0;    /* embedded NUL: not one command stream */
	if (ed_maybe_dangerous(cmds, n)) return 0;

	write_file(ROOT "/cmds", cmds, n);

	if (!redirect_streams()) return 0;

	argv[argc++] = (char *)"ed";
	if (opt_s) argv[argc++] = (char *)"-s";
	if (opt_p) { argv[argc++] = (char *)"-p"; argv[argc++] = (char *)"*"; }
	argv[argc++] = (char *)ROOT "/data";
	argv[argc] = 0;

	status = __util_ed_main(argc, argv);
	if (status != 0 && status != 1 && status != 2)
		oracle_mismatch_i("__util_ed_main returned outside {0,1,2}", cmds, status, 0);

	fflush(stdout);
	fflush(stderr);
	return 0;
}
