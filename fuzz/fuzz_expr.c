/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __util_expr_main() -- src/util/expr.c's recursive-descent evaluator for
 * expr(1p)'s grammar. Its whole input is argv (already word-split, like
 * src/sh/parse.c), so there is no separate lexer to fuzz and, unlike
 * fuzz_grep.c, no stdin/stdout `FILE *const` workaround needed.
 *
 * The fuzz buffer is split on NUL bytes into operand strings, each copied
 * into a scratch buffer this harness owns and NUL-terminates itself
 * (libFuzzer's buffer is not NUL-terminated). An empty operand (two
 * consecutive delimiters, or a leading/trailing one) is itself meaningful
 * input: it's "null" for null_or_zero(), and ":"'s operands accept an
 * empty string without rejecting the whole expression as non-numeric.
 * argv[0] is always the fixed string "expr"; only the operands after it
 * are fuzzed.
 *
 * No differential oracle against the host's own expr(1), for the same
 * reason fuzz_regex.c gives for not diffing against glibc's regexec:
 * this implementation's documented scope (C/POSIX-locale-only, no GNU
 * extensions) would make most mismatches noise, and a host `expr` binary
 * is not guaranteed present anyway. Checked instead: expr.c's own
 * documented exit-status contract of 0 (nonnull/nonzero), 1 (null or
 * zero), or 2 (invalid expression) -- its header claims no path returns
 * higher.
 *
 * src/internal/util.h requires every __util_<name>_main() to return a
 * real exit status, never call exit(), because bi_expr() (src/sh/
 * builtin.c) runs it in-process as a shell builtin with no fork -- an
 * exit() here would tear down the whole host shell over one bad `expr`
 * invocation. dupstr()/numstr() in src/util/expr.c report a failed
 * malloc() through xerr()/c->err rather than calling exit(2) directly,
 * so __util_expr_main() stays exit()-free on every path.
 *
 * Capped at 24 operands, 512 bytes of scratch shared across all of them.
 * Recursion depth in the parser is bounded by the number of "(" tokens
 * among the operands (each nested paren re-enters the whole
 * parse_or()->...->parse_primary() chain), so bounding operand count
 * bounds recursion depth too.
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
