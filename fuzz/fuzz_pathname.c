/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * basename() and dirname() -- src/misc/basename.c and src/misc/dirname.c.
 *
 * WHY THESE.  They are path-taking functions whose argument is, in every
 * real use, a name that came from somewhere else: argv, a config file, a
 * directory listing, $0.  They are also the two functions in the library
 * that do in-place pointer arithmetic on the caller's buffer -- both
 * write NULs into it, both index backwards from strlen(s) - 1, and both
 * compare a `size_t` index against a `start` offset that a drive prefix
 * moves.  A backwards index and an unsigned comparison in the same loop
 * is the shape that produces an out-of-bounds write, and neither
 * function had a harness.
 *
 * This target's versions are NOT glibc's, deliberately: they treat '\\'
 * as a separator and understand a "C:" drive prefix, which POSIX's do
 * not.  So there is no oracle here -- glibc would disagree on every
 * input containing a backslash or a colon, and that disagreement is the
 * documented design, not a defect.  What is checked instead is the set
 * of properties that hold whatever the separator conventions are.
 *
 * WHAT IS ASSERTED.
 *
 *   - NOTHING IS WRITTEN PAST THE STRING.  The buffer is malloc'd with
 *     eight guard bytes past the NUL, filled with 0xAB and checked
 *     afterwards.  ASan owns the ninth byte and everything after it; the
 *     guard is what catches a write into the slack a rounded-up malloc
 *     bucket would otherwise hide, and it also makes this harness mean
 *     something in a build without ASan.
 *
 *   - THE RESULT IS INSIDE THE BUFFER, OR IS THE LITERAL ".".  Both
 *     functions return either a pointer into the caller's string or a
 *     pointer to a static "." for the cases with no component to name.
 *     A returned pointer that is neither is a pointer the caller cannot
 *     safely read, and no sanitizer would see the caller read it.
 *
 *   - basename() RETURNS NO SEPARATOR, EXCEPT A LONE ROOT.
 *     basename.html: "If string is exactly '/', basename() shall return
 *     '/'".  Any other result naming a component must not contain a
 *     separator at all -- a returned "a/b" means the last component was
 *     not found, and every caller that appends to it builds the wrong
 *     path.
 *
 *   - dirname() NEVER LENGTHENS, AND REACHES A FIXED POINT.  Repeatedly
 *     applying dirname() is how every path walk terminates, so it must
 *     terminate: each application shortens the string or is already the
 *     fixed point ("." or a root).  The loop is bounded at len + 4 and
 *     reports if it is still shrinking after that, which is the same
 *     thing a caller's `while (strcmp(p, "/"))` loop would experience as
 *     a hang.
 *
 *   - basename() NAMES A SUFFIX OF WHAT dirname() KEPT.  Checked only in
 *     the weak form that survives the trailing-separator stripping both
 *     functions do: basename's result must appear somewhere in the
 *     original string, or be ".".  A basename that is not a substring of
 *     its input is not a component of it.
 *
 * INPUT.  The whole input is the path, with NULs mapped to '/' so a
 * fuzzer's zero bytes make separators rather than truncating the case.
 * The empty input is a legal case in its own right (both functions
 * document it: a NULL or empty string gives "."), so nothing is
 * rejected for being short.
 */
#include <string.h>
#include <stdlib.h>
#include <libgen.h>

extern void oracle_mismatch_i(const char *, const char *, long long, long long);

#define GUARD 8
#define MAXLEN 512

/* A fresh, guarded copy of the path for each call: both functions modify
 * the string in place, so one call's output cannot be another's input. */
static char *dup_guarded(const char *s, size_t len, char **base)
{
	char *b = malloc(len + 1 + GUARD);
	if (!b) return 0;
	memcpy(b, s, len);
	b[len] = 0;
	memset(b + len + 1, 0xAB, GUARD);
	*base = b;
	return b;
}

static void check_guard(const char *what, const char *in, const char *b, size_t len)
{
	size_t i;
	for (i = 0; i < GUARD; i++)
		if ((unsigned char)b[len + 1 + i] != 0xAB)
			oracle_mismatch_i(what, in, (long long)i, -1);
}

/* Either a pointer into b[0 .. len], or the static "." both files return
 * when there is no component to name. */
static void check_inside(const char *what, const char *in,
                         const char *r, const char *b, size_t len)
{
	if (!r) { oracle_mismatch_i(what, in, 0, 1); return; }
	if (r >= b && r <= b + len) return;
	if (!strcmp(r, ".")) return;
	oracle_mismatch_i(what, in, 2, 1);
}

static int issep(char c) { return c == '/' || c == '\\'; }

int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	char in[MAXLEN + 1];
	char cur[MAXLEN + 1], next[MAXLEN + 1];
	size_t len, i, steps, bound;
	char *b, *r;

	len = size < MAXLEN ? size : MAXLEN;
	memcpy(in, data, len);
	in[len] = 0;
	/* A NUL would end the path at the first zero byte and throw the rest
	 * of the input away; as a separator every byte reaches the parser. */
	for (i = 0; i < len; i++) if (!in[i]) in[i] = '/';

	/* ------------------------------------------------------ basename */
	r = 0;
	if (dup_guarded(in, len, &b)) {
		r = basename(b);
		check_guard("basename wrote past the string", in, b, len);
		check_inside("basename returned a pointer outside the buffer", in, r, b, len);
		if (r) {
			size_t rl = strlen(r);
			int seps = 0;
			for (i = 0; i < rl; i++) if (issep(r[i])) seps++;
			/* basename.html: exactly "/" comes back as "/".  Anything
			 * else that names a component must contain no separator. */
			if (seps && !(rl == 1 && issep(r[0])))
				oracle_mismatch_i("basename result contains a separator", in,
				                  (long long)seps, 0);
			if (rl > len && strcmp(r, "."))
				oracle_mismatch_i("basename result is longer than its input", in,
				                  (long long)rl, (long long)len);
			/* A component of the path, so a substring of it -- unless
			 * it is the "." both files return for "no component". */
			if (strcmp(r, ".") && rl && !strstr(in, r))
				oracle_mismatch_i("basename result is not a substring of the input",
				                  in, (long long)rl, 0);
		}
		free(b);
	}

	/* ------------------------------------------------------- dirname */
	if (dup_guarded(in, len, &b)) {
		r = dirname(b);
		check_guard("dirname wrote past the string", in, b, len);
		check_inside("dirname returned a pointer outside the buffer", in, r, b, len);
		/* "." is excused, and only ".": dirname.html says an empty
		 * string gives ".", which is one byte longer than the input it
		 * came from.  Every other result must be no longer than the
		 * path it was cut down from. */
		if (r && strcmp(r, ".") && strlen(r) > len)
			oracle_mismatch_i("dirname result is longer than its input", in,
			                  (long long)strlen(r), (long long)len);
		free(b);
	}

	/* ---------------------------------------- dirname reaches a fixed point */
	memcpy(cur, in, len + 1);
	bound = len + 4;
	for (steps = 0; steps <= bound; steps++) {
		size_t cl = strlen(cur);
		char *bb;
		if (!dup_guarded(cur, cl, &bb)) break;
		r = dirname(bb);
		check_guard("dirname wrote past the string (walk)", in, bb, cl);
		check_inside("dirname returned a pointer outside the buffer (walk)",
		             in, r, bb, cl);
		if (!r) { free(bb); break; }
		{
			size_t nl = strlen(r);
			if (nl > MAXLEN) nl = MAXLEN;
			memcpy(next, r, nl);
			next[nl] = 0;
		}
		free(bb);
		if (!strcmp(next, cur)) break;             /* the fixed point */
		if (strcmp(next, ".") && strlen(next) > strlen(cur))
			oracle_mismatch_i("dirname made the path longer", in,
			                  (long long)strlen(next), (long long)strlen(cur));
		memcpy(cur, next, strlen(next) + 1);
	}
	if (steps > bound)
		oracle_mismatch_i("repeated dirname reached no fixed point", in,
		                  (long long)steps, (long long)bound);
	return 0;
}
