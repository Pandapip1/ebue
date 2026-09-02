/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * wordexp()/wordfree() -- src/wordexp/wordexp.c plus the arithmetic
 * evaluator in src/wordexp/arith.c.  Between them they are a small
 * shell word expander: quote-state tracking, backslash escapes,
 * "$VAR", "${VAR}", "${VAR:-word}" and friends, $((expr)), field
 * splitting on IFS, and pathname expansion through glob().
 *
 * WRDE_NOCMD IS ALWAYS SET, and that is not a limitation of the
 * harness, it is the only way it can exist.  Without it a '$(' or a
 * backquote makes wordexp() run a command substitution, which forks a
 * process; a fuzzer discovers "`" in seconds and then spends its whole
 * budget forking.  With WRDE_NOCMD set, the same syntax must be
 * *diagnosed* (WRDE_CMDSUB) rather than run -- which is itself a
 * clause worth fuzzing, since the diagnosis happens in the same scanner
 * that would otherwise have to find the substitution's extent.  The
 * bit is forced on rather than left to the input for the same reason
 * fuzz_regex filters nullable repeats: an input class that reliably
 * destroys the run buries everything else.
 *
 * THE ENVIRONMENT IS SEEDED.  wordexp's parameter expansion reads
 * `environ`; against the empty-ish environment a native harness starts
 * with, every "$X" takes the undefined branch and the ${VAR:-word},
 * ${VAR:=word}, ${VAR:?word}, ${VAR:+word}, ${#VAR} and prefix/suffix
 * paths are never reached.  Six variables are set once at first call,
 * including an empty one (the ':' modifiers distinguish unset from
 * empty) and one holding glob metacharacters (so the expansion result
 * goes on to be pathname-expanded).
 *
 * THE ARITHMETIC EVALUATOR IS ALSO DRIVEN DIRECTLY.  __wordexp_arith()
 * is a full C expression parser -- unary and binary operators,
 * precedence climbing, ?:, comma, base prefixes -- and reaching it only
 * through "$((...))" means the fuzzer has to rediscover that four-byte
 * prefix before any of it is exercised.  Half of each input is handed
 * to it straight.  Its prototype is declared here rather than
 * included: it lives in src/wordexp/internal.h, which is not on this
 * harness's include path (only src/internal is).
 *
 * WHAT IS CHECKED.  No differential oracle: glibc's wordexp shells out
 * to /bin/sh, so comparing against it compares against bash, not
 * against the clause.  The properties asserted are the structural ones
 * wordexp.html states unconditionally:
 *
 *   - the return value is 0 or one of the five WRDE_* error codes;
 *   - on success we_wordv is non-NULL, we_wordv[we_offs + we_wordc] is
 *     NULL, and each of the we_wordc entries before it is a readable
 *     NUL-terminated string (read in full, so ASan catches an
 *     over-claimed we_wordc);
 *   - under WRDE_DOOFFS the we_offs leading slots are NULL;
 *   - wordfree() is safe on every outcome, and a second wordfree() is
 *     a no-op.  The WRDE_APPEND arm -- driven here for real -- is the
 *     one that moves pointers between two owners, and its file comment
 *     says the old vector is deliberately NOT freed on a non-NOSPACE
 *     error, which is exactly the kind of rule that decays into a leak
 *     or a double free.  LeakSanitizer is on for fuzz runs, so both
 *     directions are caught.
 */
#include <wordexp.h>
#include <string.h>
#include <stdlib.h>

/* src/wordexp/internal.h is not on this harness's include path. */
int __wordexp_arith(const char *expr, long *result, int flags);

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define CAP 256

/* This harness found src/wordexp/arith.c's shift operators performing
 * `cur << rhs`/`cur >> rhs` with no bound on rhs (an out-of-range count
 * is undefined behaviour, and tools/asan-build.sh's
 * -fsanitize=undefined -fno-sanitize-recover turned that into an
 * immediate process abort). Fixed in 646292ab, which bounds the count
 * and reports WRDE_SYNTAX instead (see test_wordexp_arith_shift_bounds
 * in test/posix-glob.c); this filter predates that fix and has not been
 * re-verified as removable since, so it is left in place rather than
 * dropped in a pass that cannot run the fuzz harness to check. */
static int has_shift(const char *s)
{
	size_t i;
	for (i = 0; s[i] && s[i + 1]; i++)
		if ((s[i] == '<' && s[i + 1] == '<') || (s[i] == '>' && s[i + 1] == '>'))
			return 1;
	return 0;
}

static void seed_env(void)
{
	static int done;
	if (done) return;
	done = 1;
	setenv("FOO", "bar", 1);
	setenv("EMPTY", "", 1);            /* unset vs. empty: the ':' modifiers */
	setenv("SP", "one two three", 1);  /* field splitting of an expansion */
	setenv("GL", "a*", 1);             /* expansion result then pathname-expanded */
	setenv("N", "42", 1);              /* usable inside $(( )) */
	setenv("IFS", " \t\n", 1);
}

static void check(const char *in, int flags, int rc, wordexp_t *w)
{
	size_t i, offs = (flags & WRDE_DOOFFS) ? w->we_offs : 0;

	if (rc != 0 && (rc < WRDE_BADCHAR || rc > WRDE_SYNTAX))
		oracle_mismatch_i("wordexp returned a code not in <wordexp.h>", in, rc, 0);
	if (rc != 0) return;

	if (!w->we_wordv) {
		oracle_mismatch_i("wordexp returned 0 with a NULL we_wordv", in, 0, 1);
		return;
	}
	if (w->we_wordv[offs + w->we_wordc] != NULL)
		oracle_mismatch_i("we_wordv is not NULL-terminated at we_wordc", in,
		                  (long long)w->we_wordc, 0);
	for (i = 0; i < offs; i++)
		if (w->we_wordv[i] != NULL)
			oracle_mismatch_i("WRDE_DOOFFS leading slot is not NULL", in,
			                  (long long)i, 0);
	for (i = 0; i < w->we_wordc; i++) {
		const char *s = w->we_wordv[offs + i];
		volatile char sink = 0;
		size_t j;
		if (!s) {
			oracle_mismatch_i("NULL inside the we_wordc entries", in, (long long)i, 0);
			continue;
		}
		for (j = 0; s[j]; j++) sink ^= s[j];   /* ASan sees an unterminated word */
		(void)sink;
	}
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char buf[CAP + 1], expr[CAP + 1];
	size_t n, split, elen;
	int flags, rc;
	wordexp_t w;
	long result;

	if (size < 3) return 0;
	seed_env();

	/* WRDE_NOCMD forced on; see the file banner.  WRDE_SHOWERR is
	 * masked off so a fuzzed input cannot fill the run's stderr. */
	flags = (data[0] & (WRDE_DOOFFS | WRDE_REUSE | WRDE_UNDEF)) | WRDE_NOCMD;
	split = data[1] % (size - 2);
	data += 2; size -= 2;

	n = size - split;
	if (n > CAP) n = CAP;
	memcpy(buf, data + split, n); buf[n] = 0;
	if (memchr(buf, 0, n)) return 0;
	if (has_shift(buf)) return 0;   /* reaches arith.c through "$((...))" */

	elen = split < CAP ? split : CAP;
	memcpy(expr, data, elen); expr[elen] = 0;
	if (!memchr(expr, 0, elen) && !has_shift(expr)) {
		/* The arithmetic evaluator, reached without needing the
		 * fuzzer to rediscover the "$((" prefix. */
		result = 0;
		(void)__wordexp_arith(expr, &result, flags);
	}

	memset(&w, 0, sizeof w);
	if (flags & WRDE_DOOFFS) w.we_offs = 2;

	rc = wordexp(buf, &w, flags & ~WRDE_APPEND);
	check(buf, flags & ~WRDE_APPEND, rc, &w);

	/* WRDE_APPEND onto a successful result: the ownership-transfer arm. */
	if (rc == 0) {
		int rc2 = wordexp(buf, &w, flags | WRDE_APPEND);
		check(buf, flags | WRDE_APPEND, rc2, &w);
	}

	wordfree(&w);
	wordfree(&w);                   /* must be a no-op, not a double free */
	return 0;
}
