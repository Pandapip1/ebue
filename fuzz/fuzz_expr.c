/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_expr_main() -- src/util/expr.c's recursive-descent evaluator
 * for expr(1p)'s grammar (`|` `&` `=` `>` `>=` `<` `<=` `!=` `+` `-` `*`
 * `/` `%` `:`, parens, and the numeric-candidate-vs-string coercion rule
 * that decides which of those each operand is treated as -- see that
 * file's own header comment for the full grammar table and citations).
 * Like src/sh/parse.c (fuzz_shparse.c), this is a genuine hand-written
 * parser, but its whole input is argv, not a text buffer -- there is no
 * lexer to fuzz separately, because the shell (or, here, this harness)
 * has already done the word-splitting expr's grammar needs, per this
 * project's own tokenized-argv design (expr.c's header cites
 * src/util/test.c's t_oexpr()/t_aexpr()/t_nexpr() as the precedent for
 * that shape). expr also does no file or stdin I/O at all -- its whole
 * interface is argv in, one line of stdout out -- so, unlike
 * fuzz_grep.c, there is no stdin-const-pointer workaround needed and no
 * temp file involved (include/stdio.h's `stdin`/`stdout` being
 * `FILE *const` is simply not this harness's problem).
 *
 * TURNING BYTES INTO ARGV.  The fuzz buffer is split on NUL bytes into
 * operand strings -- NUL is the obvious, safe delimiter because it can
 * never appear inside a real argv element (a real argv string is itself
 * NUL-terminated) and libFuzzer's buffer is not NUL-terminated to begin
 * with, so every operand is copied into a scratch buffer this harness
 * owns and NUL-terminates itself rather than pointed in-place at `data`.
 * Two consecutive delimiters (or a leading/trailing one) produce an
 * empty-string operand, which is itself meaningful input to this
 * grammar (an empty operand is "null" for null_or_zero()'s purposes, and
 * ":"'s left/right operands, unlike the arithmetic operators, accept an
 * empty string without rejecting the whole expression as non-numeric).
 * argv[0] is always the fixed string "expr"; only the operands after it
 * are fuzzed, matching how a real invocation's argv[0] is the utility's
 * own name (XCU 2.9.1) rather than fuzzed input.
 *
 * NO REAL DICTIONARY-STYLE SEEDING.  This grammar's operators are
 * literal ASCII tokens ("+", ">=", ":", "(", ...), and pure random-byte
 * mutation is a poor way to discover multi-token expressions that
 * exercise deep precedence chains or nested parens.  No other harness in
 * this directory ships a libFuzzer -dict either (checked: `ls
 * fuzz/*.dict` finds nothing), so this one does not add the first one;
 * coverage-guided mutation plus a corpus that accumulates over repeated
 * runs (tools/fuzz.sh's persistent $(CORPUS) tree) is what every other
 * harness here relies on for the same kind of multi-token discovery, and
 * this harness is not a special case.
 *
 * NO PRINT-BACK ORACLE EXISTS HERE, UNLIKE fuzz_shparse.c.  Checked
 * before settling for less: src/sh/parse.c builds a real AST that
 * src/sh/print.c can reprint and feed back through the parser for a
 * parse/print/reparse/print fixed-point check.  expr.c's own header
 * comment says the opposite is true by design -- "a value here is
 * evaluated exactly once, so this parser builds and evaluates in one
 * pass (no separate AST)" -- there is no tree left after
 * __util_expr_main() returns to reprint, canonicalize, or feed back in,
 * only the one printed result line and the exit status. A true
 * differential oracle (comparing against the host's own expr(1)) was
 * also considered and rejected for the same reason fuzz_regex.c's header
 * gives for not diffing against glibc's regexec: this implementation's
 * documented, deliberate scope (C/POSIX-locale-only byte comparison, no
 * GNU extensions) would make most mismatches noise about locale or
 * extension coverage rather than about a real defect, burying the rare
 * genuine one -- and there is no guarantee a host `expr` binary is even
 * present in the sandboxed build environment this harness runs in.  So
 * the bar here is what expr.c's own header comment states as this
 * implementation's complete, exact contract: "0 The expression evaluates
 * to neither null nor zero. 1 ...null or zero. 2 The expression is
 * invalid," -- and, unlike the general POSIX allowance for an
 * unspecified ">2" on some other error, this file's own header
 * explicitly claims "this file has no case that reaches >2, since none
 * of its own operations can fail for a reason other than the expression
 * itself being invalid" -- so this harness checks that claim exactly:
 * 0, 1, or 2, never anything else.
 *
 * THE exit()-VS-return DISCIPLINE.  src/internal/util.h's header states
 * every __util_<name>_main() "returns a real process exit status...
 * never a raw errno or a boolean," specifically because bi_expr()
 * (src/sh/builtin.c) runs this in-process as a shell builtin with no
 * fork -- an exit() call here would tear down the whole host shell over
 * a single bad `expr` invocation, the same class of mistake
 * src/util/dd.c's header comment documents and avoids for its own SIGINT
 * handling.  Reading src/util/expr.c in full while writing this harness
 * found exactly that mistake: dupstr(), called from every leaf of the
 * parser (every token, every arithmetic/comparison/match result) and
 * from numstr(), called exit(2) directly on a failed malloc(). Fixed
 * alongside this harness -- dupstr()/numstr() now take the parse
 * context and report an allocation failure through xerr() (setting
 * c->err, the same mechanism every other error in this file already
 * uses) and return a static one-byte sentinel buffer instead; nothing in
 * this file ever frees a dupstr()/numstr() result (per this file's own
 * header comment: values are overwritten as parsing proceeds, not
 * individually freed), so the sentinel is a safe stand-in for the
 * malloc'd string every non-OOM path returns, and __util_expr_main()
 * already checks c.err before ever printing or returning a status built
 * from that sentinel.  A real malloc() failure is not something ordinary
 * fuzzing can trigger (ASan does not fail allocations to inject this
 * path), so this was found by reading the code, not by a crash here --
 * reported per this task's own instruction to report the exit()-vs-
 * return finding either way.
 *
 * SIZE CAPS.  Up to 24 operands, 512 bytes of scratch shared across all
 * of them.  Recursion depth in the parser is bounded by the number of
 * "(" tokens among the operands (each nested paren re-enters the whole
 * parse_or()->...->parse_primary() chain, per the NOLINT(misc-no-
 * recursion) comments on each of those functions in expr.c), so bounding
 * operand count bounds recursion depth too -- 24 is far short of
 * anything that could threaten the C stack, while still being enough
 * operands to build a real multi-level expression with several levels of
 * parenthesised nesting and a full precedence chain.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/internal/util.h"

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define CAP_OPERANDS 24
#define CAP_SCRATCH 512

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char scratch[CAP_SCRATCH];
	char argv0[] = "expr";
	char *argv[CAP_OPERANDS + 2];
	int argc = 1;
	size_t si = 0, wi = 0;
	int rc;

	if (size == 0) return 0;

	argv[0] = argv0;

	while (si < size && argc < CAP_OPERANDS + 1 && wi < CAP_SCRATCH - 1) {
		size_t start = wi;

		while (si < size && data[si] != 0 && wi < CAP_SCRATCH - 1)
			scratch[wi++] = (char)data[si++];
		scratch[wi++] = 0;
		argv[argc++] = &scratch[start];

		if (si < size && data[si] == 0) si++;   /* consume the delimiter itself */
	}
	argv[argc] = NULL;

	rc = __util_expr_main(argc, argv);
	if (rc < 0 || rc > 2)
		oracle_mismatch_i("expr returned an exit status outside {0,1,2}",
		                  argc > 1 ? argv[1] : "", rc, 0);

	return 0;
}
