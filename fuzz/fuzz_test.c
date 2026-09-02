/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_test_main() -- src/util/test.c, test(1p)/[(1p)'s hand-written
 * expression evaluator: the argument-COUNT-dependent special cases
 * eval_argc() implements literally for 0-4 arguments (test(1p)'s own
 * EXTENDED DESCRIPTION, including the "$1 is '!'" and "$1 is '(' ... $N
 * is ')'" XSI cases that make a naive tokenise-then-parse
 * implementation get small argument counts wrong), and, beyond four
 * arguments, the recursive-descent grammar (t_oexpr() -> t_aexpr() ->
 * t_nexpr() -> t_primary(), '-a' left-associative and binding tighter
 * than '-o', '!' negation, parenthesised grouping) plus the unary
 * (do_unary(): file-test primaries via stat()/lstat()/access()/isatty(),
 * -n/-z string tests) and binary (do_binary(): '='/'!=' string compare,
 * -eq/-ne/-lt/-le/-gt/-ge integer compare via to_int()'s strict
 * strtol()) primaries every level of that grammar bottoms out in.
 *
 * TURNING BYTES INTO ARGV. Same shape and same reasoning as
 * fuzz_expr.c's own header comment (read it in full first: this file's
 * own header cites src/util/test.c's t_oexpr()/t_aexpr()/t_nexpr() as
 * precedent for exactly this "no separate lexer, the caller already did
 * the word-splitting" tokenized-argv design) -- the fuzz buffer, after
 * its first byte (see OPTION BYTE below), is split on NUL bytes into
 * scratch-owned, NUL-terminated operand strings, each becoming one argv
 * element. An embedded NUL is the delimiter, never smuggled into a
 * token, for the identical reason fuzz_expr.c's and fuzz_find.c's own
 * header comments give: a real argv element cannot contain one either,
 * and two consecutive delimiters (or a leading/trailing one)
 * legitimately produce an empty-string operand -- itself meaningful
 * input here, since a bare empty string is test(1p)'s own "STRING: True
 * if string is not the null string" primary failing to hold.
 *
 * OPTION BYTE. Byte 0, consumed before tokenizing, selects two bits of
 * real invocation-form coverage neither fuzz_expr.c nor fuzz_find.c has
 * any equivalent of, because neither find(1p) nor expr(1p) has a second
 * registered name with its own argument-shape rule:
 *
 *   bit 0  argv[0] is "[" instead of "test". test(1p)'s own DESCRIPTION:
 *          "In the second form ... the application shall ensure that
 *          the closing square bracket is a separate argument" --
 *          __util_test_main() itself enforces this (src/util/test.c,
 *          the `!strcmp(argv[0], "[")` block) by requiring the LAST argv
 *          element to read exactly "]", diagnosing "missing `]'"
 *          (status 2) otherwise. That check runs on whatever the
 *          tokenizer happened to produce, so it is exercised honestly in
 *          both directions rather than being hand-waved past.
 *   bit 1  ("[" mode only) force-append a literal "]" as the final argv
 *          element after tokenizing. Left clear, whether the tokenized
 *          operands happen to end in "]" is left entirely to what the
 *          fuzzer's bytes produced -- almost never, so this bit exists
 *          to reliably reach the argument-COUNT-dependent eval_argc()
 *          cases (0-4 real operands, per that function's own header
 *          comment) for "[" invocations too, rather than "[" mode being
 *          nothing but a permanent detour into the "missing `]'" error.
 *          Set OR clear, the diagnosed-error path stays reachable: clear
 *          is is the common case and mostly produces it; set still
 *          leaves an untokenizable trailing NUL or a CAP_TOKENS overflow
 *          able to push a real "]" argument out of the final slot.
 *
 * SIZE CAPS. Up to CAP_TOKENS operands (plus argv[0] and an optional
 * forced "]"), CAP_SCRATCH bytes of scratch shared across all of them --
 * the same values, and the same reasoning, fuzz_expr.c's and
 * fuzz_find.c's own headers give: recursion depth in t_oexpr()/
 * t_aexpr()/t_nexpr()/t_primary() is bounded by the number of "("
 * tokens among the operands (each nested paren re-enters the whole
 * chain, per the NOLINT(misc-no-recursion) comments on each of those
 * functions in test.c), so bounding operand count bounds recursion
 * depth too.
 *
 * REDIRECTED: stderr only, UNLIKE fuzz_expr.c, which redirects neither
 * stream despite expr.c's own diagnostics going through the identical
 * __util_diagf() mechanism. That divergence is deliberate, not an
 * oversight copied from the wrong file: a malformed or ambiguous
 * argument list is test(1p)'s single most common fuzzing outcome by
 * construction (eval_argc()'s own 0-4-argument table alone has three
 * distinct "unary/binary operator expected" diagnoses, before the
 * >4-argument grammar's own terr() calls are even reached), matching
 * fuzz_patch.c's and fuzz_uudecode.c's stated reasoning for redirecting
 * stderr rather than fuzz_expr.c's apparent tolerance of it. test(1p)
 * itself has no stdout output at all (checked while reading the file in
 * full: it only ever writes a diagnostic to stderr or returns a status;
 * there is no result line the way expr(1p) prints one), so only stderr
 * needs a seam here.
 *
 * WHAT IS CHECKED. src/internal/util.h's contract: a real exit status.
 * test(1p)'s own EXIT STATUS section gives "0 true", "1 false or null",
 * ">1 An error occurred" -- narrowed, like every other __util_*_main()
 * harness in this directory, to the exact upper bound this build's code
 * ever returns: T_ERR is literally defined as 2 (src/util/test.c) and
 * every `return` in the file (read in full) uses T_TRUE/T_FALSE/T_ERR or
 * a value derived from one of them, so 2 is the real ceiling, not merely
 * "> 1". No exit()/_exit() call anywhere in the file either; libFuzzer's
 * own atexit-based detection is the backstop, as in every other harness
 * here.
 *
 * NO ORACLE, for the same reason fuzz_expr.c's header gives for its own
 * sibling grammar: this implementation's documented, exact contract
 * (eval_argc()'s literal 0-4-argument table, is_unop()'s deliberate
 * exclusion of a unary "-a" synonym for -e) is itself the specification
 * being fuzzed, and a host `test`/`[` would disagree with some of those
 * choices by design, not by defect.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define CAP_TOKENS 24
#define CAP_SCRATCH 512
#define ROOT "/tmp/testfz"

/* ==== stderr redirection -- see this file's header comment. ============== */

static int redirect_stderr(void)
{
	return freopen(ROOT "/err", "w", stderr) != 0;
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char scratch[CAP_SCRATCH];
	/* argv[0] ("test" or "[") + up to CAP_TOKENS tokens + an optional
	 * forced "]" + the NULL terminator. */
	char *argv[CAP_TOKENS + 3];
	int argc = 0;
	size_t si, wi = 0;
	unsigned opts;
	int rc;

	if (size < 1) return 0;
	mkdir(ROOT, 0755);
	if (!redirect_stderr()) return 0;

	opts = data[0];
	data++; size--;
	si = 0;

	argv[argc++] = (opts & 0x01) ? (char *)"[" : (char *)"test";

	while (si < size && argc < CAP_TOKENS + 1 && wi < CAP_SCRATCH - 1) {
		size_t start = wi;

		while (si < size && data[si] != 0 && wi < CAP_SCRATCH - 1)
			scratch[wi++] = (char)data[si++];
		scratch[wi++] = 0;
		argv[argc++] = &scratch[start];

		if (si < size && data[si] == 0) si++;   /* consume the delimiter itself */
	}

	/* See OPTION BYTE bit 1 above -- "[" mode only, and only reachable
	 * while a slot remains for it. */
	if ((opts & 0x01) && (opts & 0x02) && argc < CAP_TOKENS + 2)
		argv[argc++] = (char *)"]";

	argv[argc] = NULL;

	rc = __util_test_main(argc, argv);
	fflush(stderr);
	if (rc < 0 || rc > 2)
		oracle_mismatch_i("test returned an exit status outside {0,1,2}",
		                  argc > 1 ? argv[1] : "", rc, 0);

	return 0;
}
