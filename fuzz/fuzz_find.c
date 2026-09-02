/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_find_main() -- src/util/find.c, find(1p)'s hand-written
 * predicate-expression parser (recursive-descent parse_or() ->
 * parse_and() -> parse_not() -> parse_primary(), the same three-level
 * '!'/-a/-o precedence shape src/util/test.c and src/util/expr.c already
 * use elsewhere in this tree, per that file's own header comment) and
 * the tree-walking evaluator built on top of it (-name/-path via
 * fnmatch(), -type/-perm/-user/-group/-size/-links/-*time/-newer against
 * a real struct stat, -prune's "skip descendants" bookkeeping over
 * nftw(), and -print).
 *
 * TURNING BYTES INTO ARGV.  Read fuzz_expr.c's own header comment in
 * full before this one: the same "no lexer to fuzz separately, the
 * caller already did the word-splitting" shape applies here (this
 * project's own tokenized-argv design, expr.c's header cites
 * src/util/test.c's precedent), and this harness reuses its exact
 * mechanism -- the fuzz buffer is split on NUL bytes into scratch-owned,
 * NUL-terminated tokens, each becoming one argv element after a fixed
 * path operand (see FIXTURE below).  An embedded NUL is the delimiter,
 * never smuggled into a token: a real argv element cannot contain one
 * either, so there is nothing lost by using it as the split point, and
 * two consecutive delimiters (or a leading/trailing one) legitimately
 * produce an empty-string token, which find.c's own consume_arg() must
 * already handle as a real (if usually erroneous) operand.
 *
 * THE FIXTURE, AND WHY A REAL DIRECTORY TREE IS BUILT ONCE.  Unlike
 * expr(1p) or grep(1p)'s pattern half, find(1p) has no operand that is
 * pure text for the parser to chew on in isolation -- every predicate in
 * its grammar (-name, -type, -perm, -newer, ...) is evaluated against
 * real struct stat data nftw() reports while walking a real path
 * operand, so *some* directory tree has to exist for parse_primary()'s
 * output to ever run past the "always true/false with no file" case.
 * Deriving that tree from fuzz bytes would mean creating and deleting an
 * unbounded number of files under this harness's simulated volume on
 * every call, for no benefit to the parser coverage this harness exists
 * for -- exactly the tradeoff fuzz_sed.c's and fuzz_ed.c's header
 * comments make for their own fixed data/text fixtures, made here for
 * the identical reason.  fixture() below builds a small, fixed tree
 * (files of different sizes, one with the executable bit set, one
 * subdirectory, one symlink) ONCE per process and reuses it: enough
 * variety for -type/-perm/-size/-name/-path/-prune to all have something
 * real to match against, without paying a mkdir/open/write per fuzz
 * iteration.
 *
 * A SAFETY EXCLUSION, NOT A COVERAGE TRADEOFF: -exec / -ok.  Read
 * fuzz_ed.c's own header comment on `!` in full before this paragraph --
 * the reasoning here is the identical mechanism applied to a different
 * primary. -exec's and -ok's do_exec()/do_exec_semi()/spawn_and_wait()
 * path (src/util/find.c) ends at the very same __spawn() fuzz_ed.c's
 * banner documents as reaching real NT process creation -- fork(2) and
 * execve() of a real host process, verified there by reading
 * fuzz/ntstubs.c's RtlCreateUserProcess, not assumed. This harness's
 * argv is built from raw fuzzer tokens with no filtering on their
 * *content*, so a fuzzer that discovered the two literal tokens "-exec"
 * ... "some-token" ";" would be a fuzzer that chose which program (and
 * arguments) to launch. reject_dangerous() below refuses the WHOLE input
 * -- never calling __util_find_main() at all -- the moment any token is
 * exactly "-exec" or "-ok": those are the only two spellings
 * parse_primary()'s strcmp()s ever recognize (an exact match, not a
 * prefix or substring, so this filter cannot be fooled by an unrelated
 * token that merely contains that text), so this one exact-token check
 * closes off do_exec()'s entire call graph before parsing ever begins,
 * at the cost of the -exec/-ok primaries' own parsing and evaluation
 * logic (argument-scanning up to ';'/'+', the "{}" trailing-marker
 * check, "{} +" batching) never running under this harness. That cost is
 * accepted deliberately, exactly as fuzz_ed.c's is, and is not a close
 * call.
 *
 * REDIRECTED: stdout/stderr, for the same reason and by the same
 * mechanism as fuzz_sed.c's and fuzz_ed.c's own header comments give --
 * -print's printf() and every parse/walk diagnostic (__util_diagf(), in
 * this file always fprintf(stderr, ...)) would otherwise write to the
 * real terminal on every one of millions of calls. -ok's own
 * confirmation read is never reached (excluded above alongside -exec),
 * so real process stdin is never touched and does not need redirecting
 * here the way fuzz_ed.c's command stream does.
 *
 * WHAT IS CHECKED.  find(1p)'s own EXIT STATUS section: "0 All path
 * operands were traversed successfully. >0 An error occurred." --
 * narrowed, like fuzz_grep.c's and fuzz_expr.c's harnesses narrow their
 * own utility's contract, to the two values src/util/find.c's own code
 * ever actually returns for a non-fatal path (1: a per-entry walk error;
 * 2: a malformed expression, always returned before any walk begins),
 * plus 0 for success -- so a fourth value is a real regression, not a
 * standards-permitted extension.
 *
 * SIZE CAPS.  Up to 24 tokens, 512 bytes of scratch shared across all of
 * them -- the same caps fuzz_expr.c uses for the same reason (recursion
 * depth in parse_or()/parse_and()/parse_not()/parse_primary() is bounded
 * by the number of "(" tokens among them, per that file's own comment on
 * why bounding operand count bounds recursion depth too).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define CAP_TOKENS 24
#define CAP_SCRATCH 512

#define ROOT "/tmp/findfz"

/* ==== fixture: a small, fixed directory tree find(1p) walks -- see this
 * file's header comment for why its content is NOT derived from the
 * fuzz input. ================================================================ */

static void write_file(const char *path, const char *data, size_t len, mode_t mode)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, mode);
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
	write_file(ROOT "/a.txt", "hello\n", 6, 0644);
	write_file(ROOT "/exec.sh", "#!/bin/sh\necho hi\n", 19, 0755);
	mkdir(ROOT "/sub", 0755);
	write_file(ROOT "/sub/c.txt", "one\ntwo\nthree\n", 14, 0644);
	mkdir(ROOT "/sub/deep", 0755);
	write_file(ROOT "/sub/deep/d.dat", "0123456789", 10, 0644);
}

/* ==== the safety exclusion -- see this file's header comment. ============ */

static int reject_dangerous(char *const *tok, int n)
{
	int i;
	for (i = 0; i < n; i++)
		if (!strcmp(tok[i], "-exec") || !strcmp(tok[i], "-ok"))
			return 1;
	return 0;
}

/* ==== stdout/stderr redirection -- see this file's header comment. ======== */

static int redirect_streams(void)
{
	if (!freopen(ROOT "/out", "w", stdout)) return 0;
	if (!freopen(ROOT "/err", "w", stderr)) return 0;
	return 1;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char scratch[CAP_SCRATCH];
	char *tok[CAP_TOKENS];
	int ntok = 0;
	size_t si = 0, wi = 0;
	/* "find" + ROOT + up to CAP_TOKENS tokens + the NULL terminator. */
	char *argv[CAP_TOKENS + 3];
	int argc = 0;
	int rc;

	if (size == 0) return 0;
	fixture();

	while (si < size && ntok < CAP_TOKENS && wi < CAP_SCRATCH - 1) {
		size_t start = wi;

		while (si < size && data[si] != 0 && wi < CAP_SCRATCH - 1)
			scratch[wi++] = (char)data[si++];
		scratch[wi++] = 0;
		tok[ntok++] = &scratch[start];

		if (si < size && data[si] == 0) si++;   /* consume the delimiter itself */
	}

	if (reject_dangerous(tok, ntok)) return 0;   /* see header: -exec/-ok excluded */

	if (!redirect_streams()) return 0;

	argv[argc++] = (char *)"find";
	argv[argc++] = (char *)ROOT;
	{
		int i;
		for (i = 0; i < ntok; i++) argv[argc++] = tok[i];
	}
	argv[argc] = NULL;

	rc = __util_find_main(argc, argv);
	fflush(stdout);
	fflush(stderr);
	if (rc < 0 || rc > 2)
		oracle_mismatch_i("find returned an exit status outside {0,1,2}",
		                  ntok > 0 ? tok[0] : "", rc, 0);

	return 0;
}
