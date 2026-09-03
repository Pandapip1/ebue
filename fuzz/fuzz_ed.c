/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fuzzes __util_ed_main()'s (src/util/ed.c) hand-written command parser.
 *
 * ed(1p) has no `-f`-style scripted-command operand: __util_ed_main()'s
 * top-level loop and its G/V step read the literal `stdin` global, so
 * this harness makes the fuzz buffer ed's command stream by freopen()ing
 * real stdin onto it each call. The one real operand, `[file]`, is a
 * small fixed fixture (not fuzzer-derived -- fuzz_regex.c already owns
 * BRE-against-arbitrary-text coverage). Byte 0 selects `-s`/`-p '*'`;
 * the rest, capped at CMD_CAP, is the command stream.
 *
 * `!` is excluded as a real safety hazard, not a coverage tradeoff:
 * ed's `!` command and the "!command" filename form of e/r/w reach
 * __spawn() -> NT process creation, and fuzz/ntstubs.c's
 * RtlCreateUserProcess is not a stub there -- it really fork()s and
 * execve()s a host process. ed_maybe_dangerous() rejects any input
 * containing a literal '!' byte, which every one of those four forms
 * requires, closing off arbitrary host command execution at the cost of
 * cmd_bang()'s own coverage -- accepted deliberately.
 *
 * SIGINT/SIGHUP delivery is not fuzzed either: __util_ed_main() installs
 * real handlers every call, but nothing here raises either signal, so
 * ed_check_interrupt()'s interrupted arm is never taken.
 *
 * No runaway-computation bound is needed: ed's grammar has no branch or
 * label construct, g/v's sub-command list is a fixed already-parsed
 * array bounded by nmatched (nested g/v is refused outright), every
 * loop's bound (ed->nlines, search scans) can only grow from the same
 * CMD_CAP-capped stream this harness already controls, the interactive
 * G/V step hits EOF on its freopen()'d regular file rather than
 * blocking, and s///'s scan strictly advances. CMD_CAP still keeps a
 * worst-case regexec() x worst-case nlines cheap in absolute terms.
 *
 * stdout/stderr are freopen()'d for the same reason as fuzz_sed.c
 * (every diagnostic and print path writes to the real streams).
 *
 * No independent oracle exists (this project's documented scope
 * narrowings -- no `W`, no `#`, single-level undo -- would read as false
 * mismatches against a real ed). Checked instead: __util_ed_main()
 * returns 0, 1, or 2 (ed's own usage-error paths return 2, unlike sed's
 * 0/1, even though this harness's own argv never triggers them), and
 * never calls exit()/_exit() -- bi_ed() runs it in-process with no fork,
 * so libFuzzer's own exit-detection is what would surface a violation.
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

/* Exact, not conservative: every path to a real subprocess requires
 * this exact byte, unlike fuzz_sed.c's approximate loop scan. */
static int ed_maybe_dangerous(const char *s, size_t n)
{
	return memchr(s, '!', n) != 0;
}

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
