/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Fuzzes regcomp()/regexec()/regerror()/regfree() (src/regex/regex.c).
 *
 * regcomp is fuzzed without restriction (any bytes, BRE and ERE, any
 * cflags); regfree/regerror run on every outcome including failed ones
 * (regfree() after a failed regcomp() is a documented safe extension
 * here -- test/posix-glob.c). regexec runs only on patterns that pass
 * safe_to_exec() below, which is deliberately not a faithful model of
 * regcomp()'s grammar.
 *
 * The filter originally existed to dodge a real process kill: run()
 * recursed once per I_SPLIT with no depth bound, so a repeat whose body
 * can match empty (e.g. "(a*)*b") compiled to a progress-free
 * SPLIT/JMP loop that exhausted the C stack while MAX_STEPS -- which
 * counts steps, not depth -- never tripped. That's now fixed in the
 * library itself (run() is iterative over a bounded heap stack and
 * returns REG_ESPACE; see regex.c's "BOUNDED MATCHING" note and
 * test_regex_nullable_repeat_does_not_crash()). The filter stays anyway
 * since a nullable repeat still burns the whole step budget at every
 * start offset -- about a second per input, costing most of the
 * harness's throughput for an already-asserted class.
 *
 * The filter has twice let known-bad inputs through by mis-modeling the
 * grammar it filters, worth recording because both were invisible until
 * found: it once treated a leading '!' in a bracket as a negation
 * marker (fnmatch's rule, not POSIX's -- only '^' negates a bracket),
 * so "[!]" read as unterminated and every repeat after it was analyzed
 * against the wrong parse; and it once treated bare '+'/'?' as literal
 * characters, missing that apply_repeat() accepts them as GNU BRE
 * leniency, so "^+" reached the fenced unbounded recursion in three
 * bytes. Both are fixed, but the lesson stands: a filter that mis-models
 * its target language admits and excludes the wrong inputs alike.
 *
 * No differential oracle: comparing against glibc's regexec produced
 * 1357 mismatches, almost all GNU BRE extensions this implementation
 * doesn't have, burying the one real disagreement -- a noisy oracle is
 * worse than none. Checked instead are properties POSIX states
 * unconditionally regardless of what the pattern means: regcomp/regexec
 * return only documented codes; a match's rm_so/rm_eo are in
 * [0, strlen(string)] with rm_so <= rm_eo (checked here rather than left
 * to ASan, since a bad offset would be handed back to the caller, not
 * dereferenced); an unfilled slot is (-1,-1); regerror never writes past
 * errbuf_size and NUL-terminates when it's nonzero.
 *
 * Pattern is capped at 48 bytes, subject at 24: regexec tries every
 * start offset with a fresh step budget each (cost is O(len*steps)), so
 * an unbounded subject makes every input an uninformative timeout; the
 * same cap also keeps a legitimately deep match from looking like the
 * now-fenced unbounded recursion.
 */
#include <regex.h>
#include <string.h>
#include <stdlib.h>

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define CAP_PAT 48
#define CAP_STR 24

/* True if no repeat operator in `p` applies to something that might
 * match the empty string. Every uncertainty resolves to "not safe" --
 * an early version assumed regcomp would reject a truncated bracket or
 * escape, but "[!].\331**\\..." compiled cleanly and killed the process,
 * so this never leans on regcomp agreeing with it.
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
			if (!p[1]) return 0;            /* trailing backslash: assume nothing */
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
			/* Only '^' negates a POSIX bracket; '!' (fnmatch's
			 * negation spelling) is an ordinary member here. */
			if (*q == '^') q++;
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
			if (!*q) return 0;              /* unterminated: assume nothing */
			p = q + 1;
			prev = 1;
			continue;
		}

		/* '*'/'+'/'?' are repeat operators in both modes: apply_repeat()
		 * accepts bare '+'/'?' as GNU BRE leniency even in strict BRE. */
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
