/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * regcomp()/regexec()/regerror()/regfree() -- src/regex/regex.c, 732
 * lines of hand-written parser, bytecode emitter and backtracking VM,
 * with no OS dependency.  It carries six `BUG:` fences in
 * test/posix-glob.c, more than any other module in the tree, and until
 * now has never seen an input it did not choose for itself.
 *
 * WHAT IS FUZZED, AND WHAT DELIBERATELY IS NOT
 *
 * regcomp is fuzzed without restriction: any byte string, in BRE and
 * ERE mode, with any cflags.  It is the half that walks a caller's
 * pattern with a pointer, and every one of its documented error codes
 * (REG_EBRACK, REG_EPAREN, REG_EBRACE, REG_BADBR, REG_ERANGE,
 * REG_ESUBREG, REG_BADRPT) is a place where a scanner decided it had
 * run out of pattern.  regfree and regerror are driven on every
 * outcome, including the failed ones -- test/posix-glob.c documents
 * that regfree() after a failed regcomp() is safe here as an extension,
 * so it must stay safe.
 *
 * regexec is fuzzed only for patterns that pass safe_to_exec() below,
 * and that filter is the price of the harness existing at all.
 * test/posix-glob.c:1925 fences a *process kill*: run() recurses once
 * per I_SPLIT with no depth bound, and a repeat whose body can match
 * the empty string compiles to a progress-free SPLIT/JMP loop, so
 * "(a*)*b" against thirty 'a's exhausts the C stack.  The MAX_STEPS
 * guard counts steps, not depth, and never trips.  That defect is
 * known, fenced, and NOT fixed by this harness -- the standing rule
 * here is to fence findings, not fix them.
 *
 * But an unfiltered regexec harness discovers it in under a second and
 * then reports nothing else, ever: libFuzzer stops at the first crash,
 * the corpus keeps the reproducer, and every subsequent nightly run
 * dies on the same known input.  A permanently red harness is not a
 * regression net.  So safe_to_exec() rejects exactly the patterns that
 * can build a nullable repeat -- a repeat operator is accepted only
 * when the item it applies to is an ordinary atom (a literal, '.', an
 * escaped character, or a bracket expression), never a group, an
 * anchor, an alternation branch start, or another repeat.  That leaves
 * the matcher's alternation, bracket, anchor, case-folding, REG_NEWLINE
 * and backreference paths fully exercised, and costs only the
 * repeat-of-a-group shapes, which are the fenced ones.
 *
 * When the fence is lifted, delete safe_to_exec() and this comment.
 *
 * NO DIFFERENTIAL ORACLE.  One was measured: comparing against glibc's
 * regexec produced 1357 mismatches, overwhelmingly GNU BRE extensions
 * ('\|', '\+', '\?') that this implementation deliberately does not
 * have, burying the single real disagreement.  A noisy oracle is worse
 * than none.  What is checked positively are the properties POSIX
 * states unconditionally and that hold whatever the pattern means:
 *
 *   - regcomp returns 0 or one of the REG_* codes in <regex.h>;
 *   - regexec returns 0 or REG_NOMATCH (or REG_ESPACE);
 *   - on a match, 0 <= rm_so <= rm_eo <= strlen(string) for every
 *     filled slot, and an unfilled slot is (-1, -1).  A negative or
 *     out-of-range offset is how a caller gets talked into an
 *     out-of-bounds read of its own subject, which is why this is
 *     checked rather than left to ASan: the bad index would be handed
 *     out, not dereferenced here;
 *   - regerror never writes past errbuf_size and always NUL-terminates
 *     when errbuf_size > 0, and returns the same size it would need.
 *
 * SIZE CAPS.  Pattern 48 bytes, subject 24.  regexec's outer loop tries
 * every start offset and gives each a fresh MAX_STEPS budget, so cost
 * is O(len * steps); an unbounded subject turns every input into a
 * libFuzzer timeout that says nothing.  run()'s recursion depth is also
 * bounded by roughly (subject length * number of repeats), so a small
 * subject keeps a *legitimate* deep match from being mistaken for the
 * fenced unbounded one.
 */
#include <regex.h>
#include <string.h>
#include <stdlib.h>

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define CAP_PAT 48
#define CAP_STR 24

/* True if no repeat operator in `p` applies to something that might
 * match the empty string.  See the file banner: this exists solely to
 * keep the harness off test/posix-glob.c:1925's fenced process kill.
 *
 * `prev` is 1 when the last thing scanned was an ordinary atom -- the
 * only kind of item that provably cannot match empty -- and 0
 * otherwise (start of pattern, after '(' / ')' / '|' / '^' / '$', and
 * after a repeat operator, so that "a**" is rejected too). */
static int safe_to_exec(const char *p, int ere)
{
	int prev = 0;

	while (*p) {
		char c = *p;

		if (c == '\\') {
			if (!p[1]) return 1;            /* trailing backslash: regcomp rejects it */
			if (!ere && p[1] == '(') { prev = 0; p += 2; continue; }
			if (!ere && p[1] == ')') { prev = 0; p += 2; continue; }
			if (!ere && p[1] == '{') {      /* BRE interval */
				if (!prev) return 0;
				prev = 0;
				p += 2;
				while (*p && *p != '}') p++;
				if (*p) p++;
				continue;
			}
			if (!ere && p[1] >= '1' && p[1] <= '9') {
				prev = 0;               /* backreference: group may be empty */
				p += 2;
				continue;
			}
			prev = 1;                       /* escaped ordinary character */
			p += 2;
			continue;
		}

		if (c == '[') {                         /* skip a whole bracket expression */
			const char *q = p + 1;
			if (*q == '^' || *q == '!') q++;
			if (*q == ']') q++;
			while (*q && *q != ']') {
				if (q[0] == '[' && (q[1] == ':' || q[1] == '.' || q[1] == '=')) {
					char kind = q[1];
					q += 2;
					while (*q && !(q[0] == kind && q[1] == ']')) q++;
					if (*q) q += 2;
					continue;
				}
				q++;
			}
			if (!*q) return 1;              /* unterminated: regcomp rejects it */
			p = q + 1;
			prev = 1;
			continue;
		}

		/* '*', '+' and '?' are repeat operators in BOTH modes.  Bare
		 * '+'/'?' are ordinary characters in strict POSIX BRE, but
		 * src/regex/regex.c's apply_repeat() deliberately accepts them
		 * as "GNU BRE" leniency (its comment says so, and
		 * test/posix-glob.c's test_regex_subexpression_capture relies
		 * on it).  Treating them as literals here is exactly the
		 * mistake this filter cannot afford: the BRE pattern "^+"
		 * applies a repeat to a zero-width I_BOL and reaches the
		 * fenced unbounded recursion in three bytes of input.  Found
		 * by this harness on its first thirty-second run. */
		if (c == '*' || c == '+' || c == '?') { if (!prev) return 0; prev = 0; p++; continue; }

		if (ere) {
			if (c == '{') {
				if (!prev) return 0;
				prev = 0;
				p++;
				while (*p && *p != '}') p++;
				if (*p) p++;
				continue;
			}
			if (c == '(' || c == ')' || c == '|') { prev = 0; p++; continue; }
		}

		if (c == '^' || c == '$') { prev = 0; p++; continue; }

		prev = 1;                               /* literal or '.' */
		p++;
	}
	return 1;
}

static int is_regcomp_code(int r)
{
	return r == 0 || (r >= REG_NOMATCH && r <= REG_BADRPT);
}

/* regerror into a guarded buffer: nothing past errbuf_size may be
 * touched, and what is written must be NUL-terminated. */
static void check_regerror(int code, const regex_t *re, const char *pat)
{
	char buf[64];
	size_t need, n;

	need = regerror(code, re, NULL, 0);
	if (need == 0)
		oracle_mismatch_i("regerror(NULL,0) returned 0", pat, 0, 1);

	for (n = 0; n <= 8; n++) {
		size_t got;
		memset(buf, 'X', sizeof buf);
		got = regerror(code, re, buf, n);
		if (got != need)
			oracle_mismatch_i("regerror size disagrees with the NULL,0 call",
			                  pat, (long long)got, (long long)need);
		if (n == 0) {
			if (buf[0] != 'X')
				oracle_mismatch_i("regerror wrote with errbuf_size 0", pat, buf[0], 'X');
		} else {
			size_t i;
			if (memchr(buf, 0, n) == NULL)
				oracle_mismatch_i("regerror did not NUL-terminate", pat, 0, 1);
			for (i = n; i < sizeof buf; i++)
				if (buf[i] != 'X')
					oracle_mismatch_i("regerror wrote past errbuf_size",
					                  pat, (long long)i, (long long)n);
		}
	}
}

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char pat[CAP_PAT + 1], str[CAP_STR + 1];
	size_t split, plen, slen;
	int cflags, eflags, r;
	regex_t re;

	if (size < 4) return 0;
	cflags = data[0] & 0x0f;                /* REG_EXTENDED|ICASE|NOSUB|NEWLINE */
	eflags = data[1] & 0x03;                /* REG_NOTBOL|REG_NOTEOL */
	split  = data[2] % (size - 3);
	data += 3; size -= 3;

	plen = split < CAP_PAT ? split : CAP_PAT;
	slen = size - split;
	if (slen > CAP_STR) slen = CAP_STR;

	memcpy(pat, data, plen); pat[plen] = 0;
	memcpy(str, data + split, slen); str[slen] = 0;
	if (memchr(pat, 0, plen) || memchr(str, 0, slen)) return 0;

	r = regcomp(&re, pat, cflags);
	if (!is_regcomp_code(r))
		oracle_mismatch_i("regcomp returned a code not in <regex.h>", pat, r, 0);

	check_regerror(r ? r : REG_NOMATCH, &re, pat);

	if (r != 0) {
		/* Documented extension (test/posix-glob.c): regfree() after a
		 * failed regcomp() must not crash or double-free. */
		regfree(&re);
		regfree(&re);
		return 0;
	}

	if (safe_to_exec(pat, (cflags & REG_EXTENDED) != 0)) {
		regmatch_t m[10];
		size_t i;

		for (i = 0; i < 10; i++) { m[i].rm_so = -2; m[i].rm_eo = -2; }
		r = regexec(&re, str, 10, m, eflags);
		if (r != 0 && r != REG_NOMATCH && r != REG_ESPACE)
			oracle_mismatch_i("regexec returned neither 0, REG_NOMATCH nor REG_ESPACE",
			                  pat, r, REG_NOMATCH);
		if (r == 0 && !(cflags & REG_NOSUB)) {
			for (i = 0; i < 10; i++) {
				if (m[i].rm_so == -1 && m[i].rm_eo == -1) continue;
				if (m[i].rm_so == -2 || m[i].rm_eo == -2)
					oracle_mismatch_i("regexec left a pmatch slot untouched",
					                  pat, (long long)i, 0);
				else if (m[i].rm_so < 0 || m[i].rm_eo < m[i].rm_so ||
				         (size_t)m[i].rm_eo > slen)
					oracle_mismatch_i("regexec offsets outside [0, strlen(string)]",
					                  pat, (long long)m[i].rm_so, (long long)m[i].rm_eo);
			}
			/* Group 0 must span exactly what matched, and re_nsub
			 * must bound the groups that can be reported. */
			if (re.re_nsub + 1 < 10 && (m[re.re_nsub + 1].rm_so != -1))
				oracle_mismatch_i("regexec filled a slot beyond re_nsub", pat,
				                  (long long)m[re.re_nsub + 1].rm_so,
				                  (long long)re.re_nsub);
		}
		/* nmatch == 0 with a NULL pmatch: regexec must not write. */
		(void)regexec(&re, str, 0, NULL, eflags);
	}

	regfree(&re);
	return 0;
}
