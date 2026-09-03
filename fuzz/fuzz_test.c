/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fuzzes __util_test_main()'s (src/util/test.c) test(1p)/[(1p) expression
 * evaluator: eval_argc()'s literal 0-4-argument special cases, and
 * beyond that the recursive-descent grammar (t_oexpr/t_aexpr/t_nexpr/
 * t_primary, '-a' binding tighter than '-o', '!' negation, parens) down
 * to its unary (file tests, -n/-z) and binary (string/integer compare)
 * primaries.
 *
 * Same tokenized-argv shape as fuzz_expr.c: the buffer, after its option
 * byte, splits on NUL into argv elements, with a NUL delimiter never
 * smuggled into a token (a real argv element can't contain one either);
 * consecutive or edge delimiters legitimately produce an empty-string
 * operand, itself meaningful since that's test(1p)'s own "STRING" test
 * failing to hold.
 *
 * Byte 0 covers invocation-form coverage neither fuzz_expr.c nor
 * fuzz_find.c needs, since neither has a second registered name: bit 0
 * makes argv[0] "[" instead of "test", exercising __util_test_main()'s
 * own "last argv element must be ']'" enforcement honestly in both
 * directions; bit 1 (in "[" mode) force-appends a literal "]" so
 * eval_argc()'s 0-4-argument cases are still reliably reached for "["
 * invocations too, rather than "[" mode being a permanent detour into
 * "missing ']'".
 *
 * CAP_TOKENS/CAP_SCRATCH use the same values and reasoning as
 * fuzz_expr.c/fuzz_find.c: t_oexpr()/t_aexpr()/t_nexpr()/t_primary()'s
 * recursion depth is bounded by the number of "(" tokens among the
 * operands, so bounding operand count bounds recursion depth too.
 *
 * Only stderr is redirected, unlike fuzz_expr.c: a malformed/ambiguous
 * argument list is test(1p)'s single most common fuzzing outcome by
 * construction, and test(1p) has no stdout output at all (checked in
 * full -- only a diagnostic or a status, no result line like expr(1p)'s).
 *
 * Checked: __util_test_main() returns 0, 1, or 2 -- T_ERR is literally
 * 2, and every return in the file uses T_TRUE/T_FALSE/T_ERR or a value
 * derived from one, so 2 is the real ceiling, narrower than XCU's
 * ">1 error". No exit()/_exit() call exists in the file either;
 * libFuzzer's own atexit detection is the backstop.
 *
 * No oracle, for the same reason as fuzz_expr.c's sibling grammar: this
 * implementation's exact documented contract (eval_argc()'s literal
 * table, is_unop()'s deliberate omissions) IS the specification being
 * fuzzed, and a host test/[ would disagree with some of it by design.
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
