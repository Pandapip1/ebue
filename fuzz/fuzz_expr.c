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
 * operand strings, each copied into a scratch buffer this harness owns
 * and NUL-terminates itself rather than pointed in-place at `data`
 * (libFuzzer's buffer is not NUL-terminated). Two consecutive delimiters
 * (or a leading/trailing one) produce an empty-string operand, which is
 * itself meaningful input to this grammar (an empty operand is "null"
 * for null_or_zero()'s purposes, and ":"'s left/right operands, unlike
 * the arithmetic operators, accept an empty string without rejecting the
 * whole expression as non-numeric). argv[0] is always the fixed string
 * "expr"; only the operands after it are fuzzed.
 *
 * No libFuzzer -dict is shipped for this grammar's literal ASCII
 * operator tokens ("+", ">=", ":", "(", ...); coverage-guided mutation
 * plus tools/fuzz.sh's persistent corpus is this directory's usual
 * answer for discovering multi-token shapes instead.
 *
 * NO PRINT-BACK ORACLE, UNLIKE fuzz_shparse.c: expr.c evaluates in one
 * pass with no separate AST (its own header: "a value here is evaluated
 * exactly once ... no separate AST"), so there is nothing left after
 * __util_expr_main() returns to reprint or feed back in. A differential
 * oracle against the host's own expr(1) was rejected for the same reason
 * fuzz_regex.c gives for not diffing against glibc's regexec: this
 * implementation's documented scope (C/POSIX-locale-only, no GNU
 * extensions) would make most mismatches noise about locale/extension
 * coverage rather than real defects, and a host `expr` binary is not
 * guaranteed present anyway. The oracle here is instead expr.c's own
 * documented exit-status contract: 0 (nonnull/nonzero), 1 (null or
 * zero), or 2 (invalid expression) -- and its header's explicit claim
 * that no path returns >2 -- so this harness checks exactly that range.
 *
 * exit()-VS-return: src/internal/util.h requires every __util_<name>_main()
 * to return a real exit status, never call exit(), because bi_expr()
 * (src/sh/builtin.c) runs it in-process as a shell builtin with no fork
 * -- an exit() here would tear down the whole host shell over one bad
 * `expr` invocation. dupstr()/numstr() in src/util/expr.c report a
 * failed malloc() through xerr()/c->err (returning a static sentinel
 * buffer that is never individually freed, matching how every other
 * dupstr()/numstr() result is handled) rather than calling exit(2)
 * directly, so __util_expr_main() stays exit()-free on every path.
 *
 * SIZE CAPS.  Up to 24 operands, 512 bytes of scratch shared across all
 * of them. Recursion depth in the parser is bounded by the number of
 * "(" tokens among the operands (each nested paren re-enters the whole
 * parse_or()->...->parse_primary() chain), so bounding operand count
 * bounds recursion depth too -- 24 is far short of anything that could
 * threaten the C stack, while still enough to build a multi-level
 * expression with several levels of nesting and a full precedence chain.
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
