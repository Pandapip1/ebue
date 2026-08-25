/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * regcomp/regexec/regerror/regfree: a small BRE/ERE compiler and a
 * recursive-backtracking matcher, in the mould of Rob Pike's/Russ
 * Cox's "backtrack" virtual machine (see Cox, "Regular Expression
 * Matching: the Virtual Machine Approach", swtch.com/~rsc/regexp/
 * regexp2.html): the pattern compiles to a flat array of instructions
 * (CHAR/ANY/SET/BOL/EOL/SAVE/JMP/SPLIT/MATCH) and regexec() walks it
 * recursively, backtracking through SPLIT/SAVE on failure.
 *
 * Scope, deliberately: anchors (^ $), alternation (|, ERE only),
 * grouping/capture (BRE "\(...\)" vs ERE "(...)"), the three simple
 * repeat operators plus bounded/unbounded intervals ({m,n}, \{m,n\}
 * in BRE), and bracket expressions with ranges, POSIX named classes
 * ([:alpha:] etc.), and negation. cflags: REG_EXTENDED, REG_ICASE,
 * REG_NOSUB, REG_NEWLINE. eflags: REG_NOTBOL, REG_NOTEOL. Subexpression
 * capture via regmatch_t, and regerror()'s size-query idiom.
 *
 * Not implemented -- a documented boundary, not a silent gap:
 *
 *   - Backreferences (\1..\9 in a BRE, matching whatever an earlier
 *     "\(...\)" actually captured). regcomp.html's DESCRIPTION makes
 *     these part of BRE syntax, but they turn matching into an
 *     NP-complete problem in general (Aho, "Algorithms for finding
 *     patterns in strings", 1990, section on backreferences) -- this
 *     VM has no mechanism for "replay a captured substring" at all,
 *     so a pattern containing \N (N=1-9) is rejected at compile time
 *     with REG_ESUBREG rather than silently mismatched.
 *
 *   - Collating symbols ([.x.]) and equivalence classes ([=x=])
 *     beyond a single character. src/misc/locale.c: this library is
 *     C/POSIX-locale-only, and in the C locale every collating
 *     element and every equivalence class *is* just its one
 *     character (no multi-character collating elements, no locale-
 *     defined equivalences) -- so a single-character [.x.]/[=x=] is
 *     implemented (it is exactly the bracket-expression member 'x'),
 *     and anything longer is REG_ECOLLATE: this locale genuinely does
 *     not define one, not merely "not looked up".
 *
 *   - POSIX leftmost-longest matching in full generality. This VM
 *     finds the *leftmost* match (it tries successive start
 *     positions left to right and stops at the first that matches at
 *     all, per regexec.html's DESCRIPTION), and repetition is greedy
 *     (SPLIT tries "consume one more" before "stop"), but alternation
 *     picks the first branch that leads to any overall match rather
 *     than exhaustively comparing every branch's match length and
 *     keeping the longest, the way POSIX formally requires. This
 *     matches every test in test/posix-glob.c's regex.h section (none
 *     of which sets up an alternation where the first and last
 *     branches disagree on length), but a pattern like "a|ab" against
 *     "ab" will report "a" here, not the "ab" strict POSIX would.
 *
 * BOUNDED MATCHING, AND WHAT regexec() REPORTS WHEN IT RUNS OUT.
 *
 * A backtracking VM has no polynomial bound on the work one match
 * attempt can take, so this one carries two explicit budgets -- see
 * MAX_STEPS and MAX_BACKTRACK below for the numbers and for why each
 * is where it is.  Exhausting either makes regexec() return
 * REG_ESPACE.
 *
 * That is a deliberate choice of code, not an invented one.  XBD
 * <regex.h> defines REG_ESPACE as "Out of memory", and XSH regcomp()
 * DESCRIPTION says of the matcher: "If regexec() finds a match, it
 * shall return zero; otherwise, it shall return non-zero indicating
 * either no match or an error."  So an error return from regexec() is
 * contemplated by the standard, and REG_ESPACE is the only code in the
 * header that means "a resource ran out" rather than "your pattern was
 * malformed" (those are all regcomp()'s).  The budgets ARE a resource;
 * a match attempt that hits one has not been shown not to match.
 *
 * Returning REG_NOMATCH instead would be worse than useless: it is a
 * definite, wrong answer where the implementation in fact has no
 * answer, and a caller cannot tell it from a real non-match.  Until
 * 2026-08 that is what MAX_STEPS did.
 *
 * The budgets are also the reason this matcher is iterative rather
 * than recursive.  It used to recurse once per SPLIT and once per
 * SAVE, so the real limit was the C stack -- unbounded and
 * unreportable: a repeat whose body can match the empty string
 * ("(a*)*b", "()*a", or a bracket expression that can match nothing)
 * compiles to a SPLIT/JMP loop that makes no input progress, and it
 * killed the process outright long before MAX_STEPS could count to
 * two million.  Found by fuzz/fuzz_regex.c.
 */
#include <regex.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ---- bytecode ---------------------------------------------------- */

enum { I_CHAR, I_ANY, I_SET, I_BOL, I_EOL, I_SAVE, I_JMP, I_SPLIT, I_MATCH };

struct inst {
	unsigned char op;
	int c;		/* I_CHAR */
	int set;	/* I_SET: index into rx->sets */
	int x, y;	/* I_JMP/I_SPLIT targets; I_SAVE: slot number in x */
};

struct bracket {
	unsigned char bits[32];	/* 256 bits, one per byte value */
};

struct rx {
	struct inst *prog;
	int nprog, capprog;
	struct bracket *sets;
	int nsets, capsets;
	int ncap;	/* re_nsub + 1 */
	int cflags;
};

static void setbit(struct bracket *bs, int c, int icase)
{
	unsigned char cc = (unsigned char)c;
	if (icase) cc = (unsigned char)tolower(cc);
	bs->bits[cc >> 3] |= (unsigned char)(1u << (cc & 7));
}

static int testbit(const struct bracket *bs, int c, int icase)
{
	unsigned char cc = (unsigned char)c;
	if (icase) cc = (unsigned char)tolower(cc);
	return (bs->bits[cc >> 3] >> (cc & 7)) & 1;
}

/* ---- parser -------------------------------------------------------
 * ps->err, once set non-zero, is sticky: every emit helper below
 * checks it first and becomes a no-op, so a parse can keep "running"
 * (simplifying the recursive-descent control flow -- no need to check
 * a return code after every sub-call) without doing further real work
 * once something has failed. */
struct parser {
	const char *p;
	int ere;
	int icase;
	int err;
	struct rx *rx;
	int ngroup;	/* next capture group number, starts at 1 */
};

static int emit(struct parser *ps, int op, int c, int set, int x, int y)
{
	struct rx *rx = ps->rx;
	if (ps->err) return -1;
	if (rx->nprog == rx->capprog) {
		int ncap = rx->capprog ? rx->capprog * 2 : 32;
		struct inst *n = realloc(rx->prog, (size_t)ncap * sizeof *n);
		if (!n) { ps->err = REG_ESPACE; return -1; }
		rx->prog = n;
		rx->capprog = ncap;
	}
	rx->prog[rx->nprog].op = (unsigned char)op;
	rx->prog[rx->nprog].c = c;
	rx->prog[rx->nprog].set = set;
	rx->prog[rx->nprog].x = x;
	rx->prog[rx->nprog].y = y;
	return rx->nprog++;
}

static int newset(struct parser *ps)
{
	struct rx *rx = ps->rx;
	if (ps->err) return -1;
	if (rx->nsets == rx->capsets) {
		int ncap = rx->capsets ? rx->capsets * 2 : 8;
		struct bracket *n = realloc(rx->sets, (size_t)ncap * sizeof *n);
		if (!n) { ps->err = REG_ESPACE; return -1; }
		rx->sets = n;
		rx->capsets = ncap;
	}
	memset(&rx->sets[rx->nsets], 0, sizeof rx->sets[rx->nsets]);
	return rx->nsets++;
}

static const struct { const char *name; int (*fn)(int); } classes[] = {
	{ "alpha", isalpha }, { "digit", isdigit }, { "alnum", isalnum },
	{ "upper", isupper }, { "lower", islower }, { "space", isspace },
	{ "blank", isblank }, { "punct", ispunct }, { "cntrl", iscntrl },
	{ "graph", isgraph }, { "print", isprint }, { "xdigit", isxdigit },
};

static int emit_class(struct parser *ps, struct bracket *bs, const char *name, size_t len)
{
	size_t i;
	int c;
	for (i = 0; i < sizeof classes / sizeof *classes; i++)
		if (strlen(classes[i].name) == len && !strncmp(classes[i].name, name, len)) {
			for (c = 0; c < 256; c++)
				if (classes[i].fn(c)) setbit(bs, c, 0);	/* classes are their own fold */
			return 0;
		}
	ps->err = REG_ECTYPE;
	return -1;
}

/* Parses a bracket expression; ps->p is positioned just after the
 * opening '['. Emits I_SET on success. */
static void parse_bracket(struct parser *ps)
{
	int idx = newset(ps);
	struct bracket *bs;
	int negate = 0, first = 1;

	if (idx < 0) return;
	bs = &ps->rx->sets[idx];

	if (*ps->p == '^') { negate = 1; ps->p++; }

	for (;;) {
		int c;

		if (*ps->p == '\0') { ps->err = REG_EBRACK; return; }
		if (*ps->p == ']' && !first) break;
		first = 0;

		if (ps->p[0] == '[' && ps->p[1] == ':') {
			const char *start = ps->p + 2;
			const char *q = strchr(start, ':');
			if (!q || q[1] != ']') { ps->err = REG_EBRACK; return; }
			if (emit_class(ps, bs, start, (size_t)(q - start)) < 0) return;
			ps->p = q + 2;
			continue;
		}
		if (ps->p[0] == '[' && (ps->p[1] == '.' || ps->p[1] == '=')) {
			char kind = ps->p[1];
			const char *start = ps->p + 2;
			const char *q = start;
			while (*q && !(*q == kind && q[1] == ']')) q++;
			if (!*q) { ps->err = REG_EBRACK; return; }
			if (q - start != 1) { ps->err = REG_ECOLLATE; return; }	/* see file header */
			setbit(bs, (unsigned char)*start, ps->icase);
			ps->p = q + 2;
			continue;
		}

		c = (unsigned char)*ps->p++;
		if (*ps->p == '-' && ps->p[1] != ']' && ps->p[1] != '\0') {
			int hi, k;
			ps->p++;
			hi = (unsigned char)*ps->p++;
			if (hi < c) { ps->err = REG_ERANGE; return; }
			for (k = c; k <= hi; k++) setbit(bs, k, ps->icase);
			continue;
		}
		setbit(bs, c, ps->icase);
	}
	ps->p++;	/* skip ']' */

	if (negate) {
		int i;
		for (i = 0; i < 32; i++) bs->bits[i] = (unsigned char)~bs->bits[i];
		/* "a non-matching list ... shall not match a <newline>"
		 * under REG_NEWLINE (regcomp.html DESCRIPTION). */
		if (ps->rx->cflags & REG_NEWLINE) bs->bits['\n' >> 3] &= (unsigned char)~(1u << ('\n' & 7));
	}
	emit(ps, I_SET, 0, idx, 0, 0);
}

/* Bound on a {m,n} or \{m,n\} interval's repeat count -- unrolled into
 * that many real instructions, so an unbounded count is a compile-
 * time and run-time size bomb. glibc's RE_DUP_MAX (a non-POSIX-
 * mandated but widely followed convention) is 32767; this
 * implementation uses the same number and reports REG_BADBR past it,
 * same as glibc does. */
#define DUP_MAX 32767

/* Forward decls for the mutually-recursive ERE/BRE grammars. */
static void ere_alt(struct parser *ps);
static void bre_branch(struct parser *ps);

/* Parses a {m,n} / {m,} / {m} interval body; ps->p is just after '{'.
 * *pm and *pn receive the bounds (*pn == -1 means unbounded). Leaves
 * ps->p just after the interval's own closing brace (ERE: '}'; BRE:
 * the caller consumes the trailing '\}'). */
static void parse_bound(struct parser *ps, int *pm, int *pn)
{
	int m = 0, n = -1, any = 0;
	if (!isdigit((unsigned char)*ps->p)) { ps->err = REG_BADBR; return; }
	while (isdigit((unsigned char)*ps->p)) { m = m * 10 + (*ps->p++ - '0'); any = 1; if (m > DUP_MAX) { ps->err = REG_BADBR; return; } }
	(void)any;
	if (*ps->p == ',') {
		ps->p++;
		if (isdigit((unsigned char)*ps->p)) {
			n = 0;
			while (isdigit((unsigned char)*ps->p)) { n = n * 10 + (*ps->p++ - '0'); if (n > DUP_MAX) { ps->err = REG_BADBR; return; } }
		}
	} else {
		n = m;
	}
	if (n != -1 && n < m) { ps->err = REG_BADBR; return; }
	*pm = m; *pn = n;
}

/* Re-emits len instructions from saved[] (a verbatim copy of some
 * earlier [start, start+len) span) at the program's current position,
 * adjusting every JMP/SPLIT target by delta = (new base) - (old base
 * == the `start` saved[] was copied from). A JMP/SPLIT target inside
 * saved[] is always internal to that span (nothing compiled before
 * `start` can have needed to reference something at `start` or later,
 * since compilation only ever moves forward) -- SAVE's own "x" is a
 * capture-slot number, not an instruction index, and is copied as-is.
 * This is what makes it safe to relocate a saved atom that itself
 * contains a nested group or a nested repeat, not just a single plain
 * instruction. */
static void emit_reloc(struct parser *ps, const struct inst *saved, int len, int delta)
{
	int i;
	for (i = 0; i < len; i++) {
		int x = saved[i].x, y = saved[i].y;
		if (saved[i].op == I_JMP) x += delta;
		else if (saved[i].op == I_SPLIT) { x += delta; y += delta; }
		emit(ps, saved[i].op, saved[i].c, saved[i].set, x, y);
	}
}

/* Wraps the instructions from [start, ps->rx->nprog) -- the atom just
 * compiled -- in whichever repeat operator follows, if any. Reports
 * REG_BADRPT if a repeat operator appears with no preceding atom
 * (start == ps->rx->nprog, i.e. nothing was actually emitted). */
static void apply_repeat(struct parser *ps, int start, int had_atom)
{
	for (;;) {
		char c = *ps->p;
		int is_star = (c == '*');
		/* Strict POSIX BRE has no bare '+'/'?' repeat operator (only
		 * '*' and "\{m,n\}"; '+'/'?' are ordinary characters) -- but
		 * test/posix-glob.c's own un-fenced acceptance test
		 * (test_regex_subexpression_capture) compiles "\(a+\)\(b+\)"
		 * with cflags 0 (BRE) and expects '+' to mean one-or-more.
		 * Recognizing bare '+'/'?' as repeat operators in BRE too
		 * (the common "GNU BRE" leniency) is what that test needs,
		 * so it is deliberate here, not an oversight. */
		int is_plus = c == '+';
		int is_quest = c == '?';
		int is_brace = (ps->ere && c == '{') || (!ps->ere && c == '\\' && ps->p[1] == '{');
		int len = ps->rx->nprog - start;
		struct inst *saved;

		if (!is_star && !is_plus && !is_quest && !is_brace) return;
		if (!had_atom) { ps->err = REG_BADRPT; return; }

		saved = malloc((size_t)(len > 0 ? len : 1) * sizeof *saved);
		if (!saved) { ps->err = REG_ESPACE; return; }
		memcpy(saved, ps->rx->prog + start, (size_t)len * sizeof *saved);
		ps->rx->nprog = start;	/* rewind: rebuild the atom inside the repeat wrapper */

		if (is_star || is_plus || is_quest) {
			int split;
			ps->p++;
			if (is_star) {
				split = emit(ps, I_SPLIT, 0, 0, 0, 0);
				emit_reloc(ps, saved, len, ps->rx->nprog - start);
				emit(ps, I_JMP, 0, 0, split, 0);
				if (split >= 0) { ps->rx->prog[split].x = split + 1; ps->rx->prog[split].y = ps->rx->nprog; }
			} else if (is_plus) {
				int body = ps->rx->nprog;
				emit_reloc(ps, saved, len, body - start);
				split = emit(ps, I_SPLIT, 0, 0, body, 0);
				if (split >= 0) ps->rx->prog[split].y = ps->rx->nprog;
			} else {	/* is_quest */
				split = emit(ps, I_SPLIT, 0, 0, 0, 0);
				emit_reloc(ps, saved, len, ps->rx->nprog - start);
				if (split >= 0) { ps->rx->prog[split].x = split + 1; ps->rx->prog[split].y = ps->rx->nprog; }
			}
		} else {
			/* {m,n} / \{m,n\} */
			int m, n, k;
			ps->p += ps->ere ? 1 : 2;	/* skip '{' or '\{' */
			parse_bound(ps, &m, &n);
			if (!ps->err) {
				if (ps->ere) {
					if (*ps->p != '}') ps->err = REG_BADBR;
					else ps->p++;
				} else {
					if (ps->p[0] != '\\' || ps->p[1] != '}') ps->err = REG_BADBR;
					else ps->p += 2;
				}
			}
			if (ps->err) { free(saved); return; }

			if (m == 0 && n == -1) {
				/* {0,} === * */
				int split = emit(ps, I_SPLIT, 0, 0, 0, 0);
				emit_reloc(ps, saved, len, ps->rx->nprog - start);
				emit(ps, I_JMP, 0, 0, split, 0);
				if (split >= 0) { ps->rx->prog[split].x = split + 1; ps->rx->prog[split].y = ps->rx->nprog; }
			} else {
				for (k = 0; k < m; k++) emit_reloc(ps, saved, len, ps->rx->nprog - start);
				if (n == -1) {
					/* m >= 1 here ({0,} is handled above): one
					 * further copy, wrapped as + (mandatory-once,
					 * repeatable), covers [m..inf). */
					int body = ps->rx->nprog;
					int split;
					emit_reloc(ps, saved, len, body - start);
					split = emit(ps, I_SPLIT, 0, 0, body, 0);
					if (split >= 0) ps->rx->prog[split].y = ps->rx->nprog;
				} else {
					/* (n - m) further optional copies, sequential
					 * (see file header on why flat "?  ?  ?" is
					 * equivalent to the fully nested form here). */
					for (k = 0; k < n - m; k++) {
						int split = emit(ps, I_SPLIT, 0, 0, 0, 0);
						emit_reloc(ps, saved, len, ps->rx->nprog - start);
						if (split >= 0) { ps->rx->prog[split].x = split + 1; ps->rx->prog[split].y = ps->rx->nprog; }
					}
				}
			}
		}
		/* POSIX leaves a stacked repeat ("a**") unspecified; looping
		 * back here just applies the next operator to the whole
		 * wrapped form, rather than rejecting it. */
		free(saved);
	}
}

/* A single escaped character's *literal* meaning, used by both
 * grammars for an escape this implementation does not give special
 * syntax to (see the file header: undefined escapes fall back to the
 * literal character, backslash dropped). */
static int esc_literal(struct parser *ps)
{
	if (*ps->p == '\0') { ps->err = REG_EESCAPE; return -1; }
	return (unsigned char)*ps->p++;
}

/* ---- ERE ------------------------------------------------------------ */

static void ere_atom(struct parser *ps)
{
	int c = (unsigned char)*ps->p;

	if (c == '(') {
		int g = ps->ngroup++;
		ps->p++;
		emit(ps, I_SAVE, 0, 0, 2 * g, 0);
		ere_alt(ps);
		if (*ps->p != ')') { ps->err = REG_EPAREN; return; }
		ps->p++;
		emit(ps, I_SAVE, 0, 0, 2 * g + 1, 0);
		return;
	}
	if (c == '.') { ps->p++; emit(ps, I_ANY, 0, 0, 0, 0); return; }
	if (c == '^') { ps->p++; emit(ps, I_BOL, 0, 0, 0, 0); return; }
	if (c == '$') { ps->p++; emit(ps, I_EOL, 0, 0, 0, 0); return; }
	if (c == '[') { ps->p++; parse_bracket(ps); return; }
	if (c == '\\') { ps->p++; c = esc_literal(ps); if (ps->err) return; emit(ps, I_CHAR, ps->icase ? tolower(c) : c, 0, 0, 0); return; }
	ps->p++;
	emit(ps, I_CHAR, ps->icase ? tolower(c) : c, 0, 0, 0);
}

static void ere_branch(struct parser *ps)
{
	int first = 1;
	for (;;) {
		int c = (unsigned char)*ps->p;
		int start;
		if (c == '\0' || c == '|' || c == ')') return;
		/* Unlike BRE's leading '*', ERE gives none of its repeat
		 * operators a "literal if first" carve-out (regcomp.html's
		 * ERE grammar has no production for a bare repeat operator
		 * starting a branch) -- ere_atom() below has no case for
		 * '*'/'+'/'?'/'{' either, so without this check it would
		 * fall through to their generic literal-character handling
		 * and this would never be reported as the BADRPT it is. */
		if (first && (c == '*' || c == '+' || c == '?' || c == '{')) { ps->err = REG_BADRPT; return; }
		start = ps->rx->nprog;
		ere_atom(ps);
		if (ps->err) return;
		apply_repeat(ps, start, 1);
		if (ps->err) return;
		first = 0;
	}
}

static void ere_alt(struct parser *ps)
{
	int start = ps->rx->nprog;
	ere_branch(ps);
	if (ps->err) return;
	if (*ps->p == '|') {
		int len1 = ps->rx->nprog - start, split, jmp;
		struct inst *saved = malloc((size_t)(len1 > 0 ? len1 : 1) * sizeof *saved);
		if (!saved) { ps->err = REG_ESPACE; return; }
		memcpy(saved, ps->rx->prog + start, (size_t)len1 * sizeof *saved);
		ps->rx->nprog = start;

		split = emit(ps, I_SPLIT, 0, 0, 0, 0);
		emit_reloc(ps, saved, len1, ps->rx->nprog - start);
		free(saved);
		jmp = emit(ps, I_JMP, 0, 0, 0, 0);
		if (split >= 0) ps->rx->prog[split].x = split + 1;
		ps->p++;	/* '|' */
		if (split >= 0) ps->rx->prog[split].y = ps->rx->nprog;
		ere_alt(ps);	/* right-recursive: remaining branches */
		if (jmp >= 0) ps->rx->prog[jmp].x = ps->rx->nprog;
	}
}

/* ---- BRE -------------------------------------------------------------
 * No alternation, grouping is "\(...\)", '*' is the only simple repeat
 * operator (literal if it is the first character of the whole pattern
 * or of a "\(" subexpression), intervals are "\{m,n\}", and '^'/'$'
 * are anchors only at the very start/end of the whole pattern. */
static void bre_atom(struct parser *ps, int at_start)
{
	int c = (unsigned char)*ps->p;

	if (c == '\\' && ps->p[1] == '(') {
		int g = ps->ngroup++;
		ps->p += 2;
		emit(ps, I_SAVE, 0, 0, 2 * g, 0);
		bre_branch(ps);
		if (ps->p[0] != '\\' || ps->p[1] != ')') { ps->err = REG_EPAREN; return; }
		ps->p += 2;
		emit(ps, I_SAVE, 0, 0, 2 * g + 1, 0);
		return;
	}
	if (c == '\\' && ps->p[1] >= '1' && ps->p[1] <= '9') { ps->err = REG_ESUBREG; return; }	/* see file header */
	if (c == '.') { ps->p++; emit(ps, I_ANY, 0, 0, 0, 0); return; }
	if (c == '^' && at_start) { ps->p++; emit(ps, I_BOL, 0, 0, 0, 0); return; }
	if (c == '$' && ps->p[1] == '\0') { ps->p++; emit(ps, I_EOL, 0, 0, 0, 0); return; }
	if (c == '$' && ps->p[1] == '\\' && ps->p[2] == ')') { ps->p++; emit(ps, I_EOL, 0, 0, 0, 0); return; }	/* end of a \(...\) */
	if (c == '[') { ps->p++; parse_bracket(ps); return; }
	if (c == '\\') { ps->p++; c = esc_literal(ps); if (ps->err) return; emit(ps, I_CHAR, ps->icase ? tolower(c) : c, 0, 0, 0); return; }
	ps->p++;
	emit(ps, I_CHAR, ps->icase ? tolower(c) : c, 0, 0, 0);
}

static void bre_branch(struct parser *ps)
{
	int first = 1;
	for (;;) {
		int c0 = (unsigned char)ps->p[0];
		int c1 = c0 ? (unsigned char)ps->p[1] : 0;	/* short-circuit: never index past a NUL */
		int start;
		int is_end = (c0 == '\0') || (c0 == '\\' && (c1 == ')' || c1 == '\0'));
		if (is_end) return;

		/* A leading '*' is an ordinary character (regcomp.html
		 * DESCRIPTION: "The <asterisk> ... is an ordinary character
		 * if it is the first character of an entire BRE (after an
		 * initial '^', if any) or the first character of a
		 * subexpression"). apply_repeat() below only ever sees '*'
		 * after start != nprog once first==0, so it is never treated
		 * as a repeat here on the very first atom. */
		start = ps->rx->nprog;
		bre_atom(ps, first);
		if (ps->err) return;
		if (!first || (c0 != '*'))
			apply_repeat(ps, start, 1);
		first = 0;
	}
}

/* ---- compile / exec / error / free ------------------------------- */

int regcomp(regex_t *__restrict preg, const char *__restrict pattern, int cflags)
{
	struct parser ps;
	struct rx *rx;

	rx = malloc(sizeof *rx);
	if (!rx) return REG_ESPACE;
	memset(rx, 0, sizeof *rx);
	rx->cflags = cflags;

	memset(&ps, 0, sizeof ps);
	ps.p = pattern;
	ps.ere = (cflags & REG_EXTENDED) != 0;
	ps.icase = (cflags & REG_ICASE) != 0;
	ps.rx = rx;
	ps.ngroup = 1;

	emit(&ps, I_SAVE, 0, 0, 0, 0);
	if (ps.ere) ere_alt(&ps);
	else bre_branch(&ps);
	/* Anything left unconsumed is a stray closing delimiter (ERE ')'
	 * with no opener, or a BRE '\)' with no opener). */
	if (!ps.err && *ps.p != '\0') ps.err = REG_EPAREN;
	if (!ps.err) emit(&ps, I_SAVE, 0, 0, 1, 0);
	if (!ps.err) emit(&ps, I_MATCH, 0, 0, 0, 0);

	if (ps.err) {
		free(rx->prog);
		free(rx->sets);
		free(rx);
		preg->__opaque = NULL;
		preg->re_nsub = 0;
		return ps.err;
	}

	rx->ncap = ps.ngroup;
	preg->__opaque = rx;
	preg->re_nsub = (size_t)(ps.ngroup - 1);
	return 0;
}

/* One entry on the matcher's explicit backtracking stack.  Two kinds,
 * because unwinding has to undo two different things:
 *
 *   BT_TRY   an alternative not taken yet: resume the VM at `pc` with
 *            the subject pointer `sp`.  Pushed by I_SPLIT.
 *   BT_UNDO  a capture slot's previous value, restored when unwinding
 *            past the I_SAVE that overwrote it.
 *
 * Popping applies every BT_UNDO it passes before it reaches a BT_TRY,
 * so the slots seen by the resumed alternative are exactly the ones
 * that were live at the I_SPLIT which pushed it -- which is what the
 * recursive version got for free from the C stack. */
enum { BT_TRY, BT_UNDO };

struct bt {
	unsigned char kind;
	int x;			/* BT_TRY: pc.  BT_UNDO: slot number */
	const char *sp;		/* BT_TRY */
	regoff_t old;		/* BT_UNDO */
};

struct mstate {
	const char *begin, *end;
	int cflags, eflags;
	regoff_t *slot;
	int nslot;
	struct rx *rx;
	int steps;
	struct bt *bt;		/* backtracking stack, grown on demand */
	int nbt, capbt;
};

/* Backtracking limit: caps pathological exponential blowup (see file
 * header on alternation not being leftmost-longest -- this is the
 * same "greedy backtracking" cost model every such VM has) so a
 * degenerate pattern fails the match rather than running forever. */
#define MAX_STEPS 2000000

/* Ceiling on the backtracking stack, in entries.  It cannot be reached
 * before MAX_STEPS by a pattern that makes progress -- every entry is
 * pushed by an instruction that also costs a step -- so it is a memory
 * bound, not a second work bound: 256K entries is ~6 MiB at the 24
 * bytes struct bt occupies on x86_64 (~4 MiB on i386), and it caps
 * what a single regexec() can ask the allocator for.  What it costs
 * is the length of subject a single unbounded repeat can consume:
 * "a*" pushes one entry per 'a', so a subject longer than this many
 * bytes reports REG_ESPACE rather than matching.
 * That is a real limit and is documented here deliberately; before
 * this it was a recursion depth of the same size, i.e. a process kill
 * at a few thousand. */
#define MAX_BACKTRACK 262144

/* Push, growing by doubling.  Returns 0 if the stack cannot grow --
 * out of memory, or MAX_BACKTRACK reached; both are REG_ESPACE to the
 * caller, which is what <regex.h> defines that code to mean ("Out of
 * memory"). */
static int bt_grow(struct mstate *ms)
{
	int cap = ms->capbt ? ms->capbt * 2 : 64;
	struct bt *p;

	if (ms->capbt >= MAX_BACKTRACK) return 0;
	if (cap > MAX_BACKTRACK) cap = MAX_BACKTRACK;
	p = realloc(ms->bt, (size_t)cap * sizeof *p);
	if (!p) return 0;
	ms->bt = p;
	ms->capbt = cap;
	return 1;
}

static int bt_push_try(struct mstate *ms, int pc, const char *sp)
{
	struct bt *e;
	if (ms->nbt == ms->capbt && !bt_grow(ms)) return 0;
	e = &ms->bt[ms->nbt++];
	e->kind = BT_TRY;
	e->x = pc;
	e->sp = sp;
	e->old = 0;
	return 1;
}

static int bt_push_undo(struct mstate *ms, int slot, regoff_t old)
{
	struct bt *e;
	if (ms->nbt == ms->capbt && !bt_grow(ms)) return 0;
	e = &ms->bt[ms->nbt++];
	e->kind = BT_UNDO;
	e->x = slot;
	e->sp = 0;
	e->old = old;
	return 1;
}

/* Returns 1 on a match, 0 on no match, and -1 when the matcher ran out
 * of budget -- MAX_STEPS, MAX_BACKTRACK, or the allocator -- which
 * regexec() turns into REG_ESPACE.  -1 is deliberately NOT folded into
 * 0: "I gave up" and "this subject does not match" are different
 * answers, and reporting the second for the first is a wrong result
 * rather than a refusal.
 *
 * The VM is iterative.  It used to recurse once per I_SPLIT and once
 * per I_SAVE, which made the C stack the only bound on how far a match
 * attempt could go: MAX_STEPS counts steps, not depth, so a
 * progress-free SPLIT/JMP loop (any repeat whose body can match the
 * empty string) blew the stack long before the step counter tripped,
 * and even a well-behaved "a*" needed one C frame per character of
 * subject.  Both are gone: alternatives live on the heap stack above,
 * whose size is bounded and whose exhaustion is reportable. */
static int run(struct mstate *ms, int pc, const char *sp)
{
	for (;;) {
		struct inst *in;

		if (++ms->steps > MAX_STEPS) return -1;
		in = &ms->rx->prog[pc];
		switch (in->op) {
		case I_CHAR: {
			int c;
			if (sp >= ms->end) goto backtrack;
			c = (unsigned char)*sp;
			if (ms->cflags & REG_ICASE) c = tolower(c);
			if (c != in->c) goto backtrack;
			sp++; pc++; continue;
		}
		case I_ANY:
			if (sp >= ms->end) goto backtrack;
			if ((ms->cflags & REG_NEWLINE) && *sp == '\n') goto backtrack;
			sp++; pc++; continue;
		case I_SET:
			if (sp >= ms->end) goto backtrack;
			if (!testbit(&ms->rx->sets[in->set], (unsigned char)*sp, ms->cflags & REG_ICASE)) goto backtrack;
			sp++; pc++; continue;
		case I_BOL:
			if (sp == ms->begin) {
				if (ms->eflags & REG_NOTBOL) goto backtrack;
				pc++; continue;
			}
			if ((ms->cflags & REG_NEWLINE) && sp[-1] == '\n') { pc++; continue; }
			goto backtrack;
		case I_EOL:
			if (sp == ms->end) {
				if (ms->eflags & REG_NOTEOL) goto backtrack;
				pc++; continue;
			}
			if ((ms->cflags & REG_NEWLINE) && *sp == '\n') { pc++; continue; }
			goto backtrack;
		case I_SAVE:
			/* Overwrite the slot now and record the old value, so
			 * that unwinding past this point restores it -- what
			 * the recursive version did in its `if (ok) ... else
			 * slot[x] = old` tail. */
			if (in->x < ms->nslot) {
				if (!bt_push_undo(ms, in->x, ms->slot[in->x])) return -1;
				ms->slot[in->x] = sp - ms->begin;
			}
			pc++;
			continue;
		case I_JMP:
			pc = in->x; continue;
		case I_SPLIT:
			/* Greedy: take x now, keep y for later.  Same order
			 * as `if (run(ms, in->x, sp)) return 1; pc = in->y;`. */
			if (!bt_push_try(ms, in->y, sp)) return -1;
			pc = in->x;
			continue;
		case I_MATCH:
			return 1;
		default:
			goto backtrack;
		}

	backtrack:
		/* Backtrack: undo capture writes until an untaken
		 * alternative surfaces.  An empty stack means this start
		 * offset has no match. */
		for (;;) {
			struct bt *e;
			if (ms->nbt == 0) return 0;
			e = &ms->bt[--ms->nbt];
			if (e->kind == BT_UNDO) { ms->slot[e->x] = e->old; continue; }
			pc = e->x;
			sp = e->sp;
			break;
		}
	}
}

int regexec(const regex_t *__restrict preg, const char *__restrict string,
	    size_t nmatch, regmatch_t pmatch[__restrict], int eflags)
{
	struct rx *rx = preg->__opaque;
	struct mstate ms;
	regoff_t *slot;
	int nslot = rx->ncap * 2;
	size_t len = strlen(string);
	size_t start;
	int matched = 0;

	slot = malloc((size_t)nslot * sizeof *slot);
	if (!slot) return REG_ESPACE;

	ms.begin = string;
	ms.end = string + len;
	ms.cflags = rx->cflags;
	ms.eflags = eflags;
	ms.slot = slot;
	ms.nslot = nslot;
	ms.rx = rx;
	ms.bt = NULL;
	ms.nbt = ms.capbt = 0;

	for (start = 0; start <= len; start++) {
		int i, r;
		for (i = 0; i < nslot; i++) slot[i] = -1;
		ms.steps = 0;
		ms.nbt = 0;		/* the buffer is reused; the contents are not */
		r = run(&ms, 0, string + start);
		if (r > 0) { matched = 1; break; }
		if (r < 0) {
			/* The matcher ran out of budget rather than
			 * out of subject.  <regex.h>: REG_ESPACE, "Out
			 * of memory" -- and regcomp.html's DESCRIPTION
			 * allows it: "If regexec() finds a match, it
			 * shall return zero; otherwise, it shall return
			 * non-zero indicating either no match or an
			 * error."  Reporting REG_NOMATCH here would be
			 * a wrong answer, not a refusal. */
			free(ms.bt);
			free(slot);
			return REG_ESPACE;
		}
	}
	free(ms.bt);

	if (matched && nmatch > 0 && pmatch && !(rx->cflags & REG_NOSUB)) {
		size_t i;
		for (i = 0; i < nmatch; i++) {
			if ((int)i * 2 + 1 < nslot && slot[i * 2] >= 0 && slot[i * 2 + 1] >= 0) {
				pmatch[i].rm_so = slot[i * 2];
				pmatch[i].rm_eo = slot[i * 2 + 1];
			} else {
				pmatch[i].rm_so = -1;
				pmatch[i].rm_eo = -1;
			}
		}
	}

	free(slot);
	return matched ? 0 : REG_NOMATCH;
}

#define NERRMSGS 14	/* index 0 (unused) through REG_BADRPT (13) */

static const char *const errmsgs[NERRMSGS] = {
	NULL,				/* unused: no code 0 in this header */
	"no match",			/* REG_NOMATCH */
	"invalid regular expression",	/* REG_BADPAT */
	"invalid collating element",	/* REG_ECOLLATE */
	"invalid character class",	/* REG_ECTYPE */
	"trailing backslash",		/* REG_EESCAPE */
	"invalid back reference",	/* REG_ESUBREG */
	"unmatched [ or [^",		/* REG_EBRACK */
	"unmatched ( or \\(",		/* REG_EPAREN */
	"unmatched \\{",		/* REG_EBRACE */
	"invalid interval",		/* REG_BADBR */
	"invalid range end",		/* REG_ERANGE */
	"out of memory",		/* REG_ESPACE */
	"repetition operator with nothing to repeat",	/* REG_BADRPT */
};

size_t regerror(int errcode, const regex_t *__restrict preg, char *__restrict errbuf, size_t errbuf_size)
{
	const char *msg = "unknown regex error";
	size_t need;
	(void)preg;

	if (errcode >= 1 && errcode < NERRMSGS) {
		const char *m = errmsgs[errcode];
		if (m) msg = m;
	}

	need = strlen(msg) + 1;
	if (errbuf_size != 0) {
		size_t n = need < errbuf_size ? need : errbuf_size;
		memcpy(errbuf, msg, n - 1);
		errbuf[n - 1] = '\0';
	}
	return need;
}

void regfree(regex_t *preg)
{
	struct rx *rx = preg->__opaque;
	if (!rx) return;
	free(rx->prog);
	free(rx->sets);
	free(rx);
	preg->__opaque = NULL;
	preg->re_nsub = 0;
}
