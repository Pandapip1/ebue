/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * $((expr)): POSIX arithmetic expansion (XBD 2.6.4), which -- unlike
 * command substitution -- is not actually a shell feature this
 * platform lacks. 2.6.4 says the expression, after parameter
 * expansion/command substitution/quote removal have already run over
 * its tokens, "shall be processed according to the rules given in
 * Arithmetic Precision and Operations" (XBD 1.1.2): signed long
 * arithmetic, the ISO C standard's expression grammar and operator
 * semantics (C11 6.5, referenced by 1.1.2's "The evaluation of
 * arithmetic expressions shall be equivalent to that described in
 * Section 6.5, Expressions, of the ISO C standard"), minus three
 * things 2.6.4 explicitly drops: sizeof(), prefix/postfix ++/--, and
 * "selection, iteration, and jump statements" (irrelevant here anyway
 * -- an expression, not a program, is all $((...)) ever contains).
 * Constants: only decimal/octal/hexadecimal per ISO C 6.4.4.1, which
 * strtol(..., 0) already recognizes.
 *
 * A recursive-descent, precedence-climbing evaluator over the C
 * grammar minus the excluded pieces above: comma, assignment (=, and
 * the compound forms +=, -=, etc. -- 1.1.2's operator table lists
 * "standard and compound assignments" as required), ?:, ||, &&, |, ^,
 * &, ==/!=, relational, shift, additive, multiplicative, unary
 * +/-/~/!, and primary (a parenthesized sub-expression, a constant, or
 * an identifier).
 *
 * "Shell variable": 2.6.4 -- "if the shell variable x contains a value
 * that forms a valid integer constant ... $((x)) and $(($x)) shall
 * return the same value" -- says a bare identifier and a $-prefixed
 * one must agree, and "All changes to variables in an arithmetic
 * expression shall be in effect after the arithmetic expansion" says
 * assignment inside the expression is a real, persistent side effect.
 * This implementation's only notion of "shell variable" anywhere (see
 * wordexp.c's expand_param()) is the process environment via getenv(),
 * so that is what a bare identifier reads and an assignment writes
 * (setenv()). A leading '$' before a name is recognized right here at
 * the lexical level and treated identically to the bare form (both
 * read/write the same getenv()/setenv() slot), which gives the
 * "$((x)) and $(($x)) ... same value" guarantee directly. This also
 * makes $NAME a valid assignment target ($(($x=1)) works here exactly
 * like $((x=1))), which is a harmless superset of what a real shell
 * accepts -- 2.6.4 never requires $NAME to be rejected as an lvalue,
 * only that unprefixed and prefixed *reads* agree.
 *
 * Short-circuiting (||, &&, and the untaken arm of ?:) is real: a
 * `live` flag threaded through every level suppresses evaluation
 * errors (division/modulus by zero, an undefined variable under
 * WRDE_UNDEF) and assignment side effects while it is false, exactly
 * like a real shell not touching `y` in "$((0 && (y = 1) ))". Tokens
 * on the untaken side are still *parsed* (so trailing syntax errors
 * are still caught), just not *evaluated* for effect.
 *
 * No dedicated WRDE_* code exists for "the arithmetic expression itself
 * is invalid" -- <wordexp.h>'s five WRDE_* values were fixed by
 * test/posix-glob.c before command/arithmetic substitution were
 * genuine gaps (see include/wordexp.h's own note on this) and the
 * spec's own wording for the general case, 2.6.4's "the expansion
 * fails and the shell shall write a diagnostic message", never named
 * one either. WRDE_SYNTAX ("shell syntax error") is the nearest
 * documented fit and is what every failure path below reports unless
 * noted otherwise: a malformed expression, trailing garbage after a
 * complete one, a division/modulus by zero, or (2.6.4: "the contents
 * of a shell variable used in the expression are not recognized by
 * the shell") a $NAME/bare-name whose value is not itself a valid
 * integer constant. The one case with a better fit is WRDE_BADVAL,
 * already defined as "[r]eference to undefined shell variable when
 * WRDE_UNDEF is set" -- reused verbatim for an undefined variable read
 * inside the expression when the caller passed that flag, matching
 * expand_param()'s treatment of a bare $NAME everywhere else in this
 * module. A setenv() failure (out of memory) is reported as
 * WRDE_NOSPACE, same as every other allocation failure in this
 * directory.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "internal.h"
#include "libc.h"

struct arith {
	const char *p;
	int flags;
	int err;	/* first WRDE_* failure seen, or 0 */
	int live;	/* 0 inside a short-circuited/untaken branch: parse, don't evaluate */
};

static void fail(struct arith *a, int code)
{
	if (a->live && !a->err) a->err = code;
}

static void skip_ws(struct arith *a)
{
	while (*a->p == ' ' || *a->p == '\t' || *a->p == '\n') a->p++;
}

static int is_namestart(char c) { return isalpha((unsigned char)c) || c == '_'; }
static int is_namechar(char c) { return isalnum((unsigned char)c) || c == '_'; }

/* Parses a NAME at *pp (namestart already confirmed) into buf (must be
 * at least 256 bytes); advances *pp past it. Returns 1 on success, 0 if
 * the name is too long to fit (a WRDE_SYNTAX condition, matching
 * expand_param()'s same-sized buffer in wordexp.c). */
static int read_name(const char **pp, char *buf)
{
	const char *p = *pp, *start = p;
	size_t len;
	while (is_namechar(*p)) p++;
	len = (size_t)(p - start);
	if (len >= 256) return 0;
	memcpy(buf, start, len);
	buf[len] = 0;
	*pp = p;
	return 1;
}

/* Parses a full string as a "valid integer constant" (2.6.4's phrase
 * for a shell variable's contents): optional surrounding whitespace,
 * then whatever strtol(..., 0) accepts, consuming the whole string.
 * Used for both getenv() values (2.6.4's own wording, quoted above)
 * and for the constants that appear directly in the expression text
 * (XBD 1.1.2 -- "decimal-constant, octal-constant, and
 * hexadecimal-constant ... required to be recognized", exactly what
 * base-0 strtol recognizes). */
static int parse_int_const(const char *s, long *out)
{
	char *end;
	long v;
	while (*s == ' ' || *s == '\t' || *s == '\n') s++;
	if (!*s) return 0;
	v = strtol(s, &end, 0);
	while (*end == ' ' || *end == '\t' || *end == '\n') end++;
	if (*end) return 0;
	*out = v;
	return 1;
}

/* Reads a shell variable (this implementation's environ) by name.
 * Under WRDE_UNDEF, an unset variable is a failure (WRDE_BADVAL); left
 * unset otherwise, it is 0 -- what every shell does for an unset
 * variable in arithmetic context, and 2.6.4 does not forbid. A value
 * that is not itself a valid integer constant is always a failure
 * (2.6.4: "the contents of a shell variable used in the expression are
 * not recognized by the shell, the expansion fails"). fail() itself
 * already no-ops while !a->live, so a suppressed branch's reads are
 * free to be wrong without side effects. */
static long read_var(struct arith *a, const char *name)
{
	const char *val = getenv(name);
	long v;
	if (!val) {
		if (a->flags & WRDE_UNDEF) fail(a, WRDE_BADVAL);
		return 0;
	}
	if (!parse_int_const(val, &v)) {
		fail(a, WRDE_SYNTAX);
		return 0;
	}
	return v;
}

/* Formats v as a decimal string and setenv()s it -- the persistent
 * side effect 2.6.4 requires ("All changes to variables in an
 * arithmetic expression shall be in effect after the arithmetic
 * expansion"). A no-op inside a suppressed branch. */
static void assign_var(struct arith *a, const char *name, long v)
{
	/* sign + up to 20 digits (64-bit LONG_MIN) + NUL, rounded up */
	char buf[32];
	int n = 0, i, j;
	unsigned long u;
	int neg = v < 0;

	if (!a->live) return;
	u = neg ? (unsigned long)(-(v + 1)) + 1UL : (unsigned long)v;
	if (u == 0) buf[n++] = '0';
	while (u) { buf[n++] = (char)('0' + (u % 10)); u /= 10; }
	if (neg) buf[n++] = '-';
	buf[n] = 0;
	for (i = 0, j = n - 1; i < j; i++, j--) { char t = buf[i]; buf[i] = buf[j]; buf[j] = t; }
	if (setenv(name, buf, 1) < 0) fail(a, WRDE_NOSPACE);
}

/* '=' alone (not '==') plus every compound-assignment spelling XBD
 * 1.1.2's operator table lists ("standard and compound assignments").
 * On a match, returns a code for the underlying binary operator ('='
 * for plain assignment) and advances *pp past the whole token; on no
 * match, returns 0 and *pp is unchanged. Three-character spellings are
 * tried first so "<<=" is never mistaken for "<" followed by "<=". */
static int match_assign_op(const char **pp)
{
	static const char *const three[] = { "<<=", ">>=", 0 };
	static const char *const two[] = { "+=", "-=", "*=", "/=", "%=", "&=", "^=", "|=", 0 };
	int i;
	for (i = 0; three[i]; i++)
		if (!strncmp(*pp, three[i], 3)) { *pp += 3; return three[i][0] == '<' ? 'L' : 'R'; }
	for (i = 0; two[i]; i++)
		if (!strncmp(*pp, two[i], 2)) { *pp += 2; return two[i][0]; }
	if (**pp == '=' && (*pp)[1] != '=') { *pp += 1; return '='; }
	return 0;
}

static long apply_binop(struct arith *a, int op, long cur, long rhs)
{
	switch (op) {
	case '=': return rhs;
	case '+': return cur + rhs;
	case '-': return cur - rhs;
	case '*': return cur * rhs;
	case '/': if (rhs == 0) { fail(a, WRDE_SYNTAX); return 0; } return cur / rhs;
	case '%': if (rhs == 0) { fail(a, WRDE_SYNTAX); return 0; } return cur % rhs;
	case '&': return cur & rhs;
	case '^': return cur ^ rhs;
	case '|': return cur | rhs;
	case 'L': return cur << rhs;	/* "<<="/"<<" */
	case 'R': return cur >> rhs;	/* ">>="/">>" */
	case '<': return cur < rhs;
	case '>': return cur > rhs;
	case 'l': return cur <= rhs;	/* "<=" */
	case 'g': return cur >= rhs;	/* ">=" */
	case 'e': return cur == rhs;	/* "==" */
	case 'n': return cur != rhs;	/* "!=" */
	}
	return 0; /* unreachable */
}

static long arith_assign(struct arith *a);
static long arith_cond(struct arith *a);

/* primary: '(' assignment-expression ')' | constant | [$]NAME */
static long arith_primary(struct arith *a)
{
	long v;
	skip_ws(a);
	if (*a->p == '(') {
		a->p++;
		v = arith_assign(a);
		skip_ws(a);
		if (*a->p == ')') a->p++;
		else fail(a, WRDE_SYNTAX);
		return v;
	}
	if (*a->p == '$' && is_namestart(a->p[1])) {
		char name[256];
		const char *p = a->p + 1;
		if (!read_name(&p, name)) { fail(a, WRDE_SYNTAX); a->p = p; return 0; }
		a->p = p;
		return read_var(a, name);
	}
	if (is_namestart(*a->p)) {
		char name[256];
		if (!read_name(&a->p, name)) { fail(a, WRDE_SYNTAX); return 0; }
		return read_var(a, name);
	}
	if (isdigit((unsigned char)*a->p)) {
		char *end;
		v = strtol(a->p, &end, 0);
		a->p = end;
		return v;
	}
	fail(a, WRDE_SYNTAX);
	if (*a->p) a->p++;	/* don't get stuck on an unrecognized byte */
	return 0;
}

/* unary: ('+' | '-' | '~' | '!')* primary */
static long arith_unary(struct arith *a)
{
	skip_ws(a);
	if (*a->p == '+') { a->p++; return arith_unary(a); }
	if (*a->p == '-') { a->p++; return -arith_unary(a); }
	if (*a->p == '~') { a->p++; return ~arith_unary(a); }
	if (*a->p == '!') { a->p++; return !arith_unary(a); }
	return arith_primary(a);
}

/* multiplicative: unary (('*'|'/'|'%') unary)* */
static long arith_mul(struct arith *a)
{
	long v = arith_unary(a);
	for (;;) {
		int op;
		skip_ws(a);
		if (*a->p == '*') op = '*';
		else if (*a->p == '/') op = '/';
		else if (*a->p == '%') op = '%';
		else return v;
		a->p++;
		v = apply_binop(a, op, v, arith_unary(a));
	}
}

/* additive: multiplicative (('+'|'-') multiplicative)* */
static long arith_add(struct arith *a)
{
	long v = arith_mul(a);
	for (;;) {
		int op;
		skip_ws(a);
		if (*a->p == '+') op = '+';
		else if (*a->p == '-') op = '-';
		else return v;
		a->p++;
		v = apply_binop(a, op, v, arith_mul(a));
	}
}

/* shift: additive (('<<'|'>>') additive)* */
static long arith_shift(struct arith *a)
{
	long v = arith_add(a);
	for (;;) {
		int op;
		skip_ws(a);
		if (a->p[0] == '<' && a->p[1] == '<') op = 'L';
		else if (a->p[0] == '>' && a->p[1] == '>') op = 'R';
		else return v;
		a->p += 2;
		v = apply_binop(a, op, v, arith_add(a));
	}
}

/* relational: shift (('<='|'>='|'<'|'>') shift)* -- two-character
 * spellings checked first so "<=" is never read as "<" then "=". */
static long arith_rel(struct arith *a)
{
	long v = arith_shift(a);
	for (;;) {
		int op;
		skip_ws(a);
		if (a->p[0] == '<' && a->p[1] == '=') { op = 'l'; a->p += 2; }
		else if (a->p[0] == '>' && a->p[1] == '=') { op = 'g'; a->p += 2; }
		else if (a->p[0] == '<') { op = '<'; a->p += 1; }
		else if (a->p[0] == '>') { op = '>'; a->p += 1; }
		else return v;
		v = apply_binop(a, op, v, arith_shift(a));
	}
}

/* equality: relational (('=='|'!=') relational)* */
static long arith_eq(struct arith *a)
{
	long v = arith_rel(a);
	for (;;) {
		int op;
		skip_ws(a);
		if (a->p[0] == '=' && a->p[1] == '=') op = 'e';
		else if (a->p[0] == '!' && a->p[1] == '=') op = 'n';
		else return v;
		a->p += 2;
		v = apply_binop(a, op, v, arith_rel(a));
	}
}

/* bitwise-AND: equality ('&' equality)* -- "&&" must never be split
 * into two bitwise-ANDs, so a lone '&' is only consumed when the next
 * byte is not itself '&'. */
static long arith_band(struct arith *a)
{
	long v = arith_eq(a);
	for (;;) {
		skip_ws(a);
		if (a->p[0] != '&' || a->p[1] == '&') return v;
		a->p++;
		v = apply_binop(a, '&', v, arith_eq(a));
	}
}

/* bitwise-XOR: bitwise-AND ('^' bitwise-AND)* */
static long arith_bxor(struct arith *a)
{
	long v = arith_band(a);
	for (;;) {
		skip_ws(a);
		if (*a->p != '^') return v;
		a->p++;
		v = apply_binop(a, '^', v, arith_band(a));
	}
}

/* bitwise-OR: bitwise-XOR ('|' bitwise-XOR)* -- same "don't split ||"
 * guard as arith_band() above. */
static long arith_bor(struct arith *a)
{
	long v = arith_bxor(a);
	for (;;) {
		skip_ws(a);
		if (a->p[0] != '|' || a->p[1] == '|') return v;
		a->p++;
		v = apply_binop(a, '|', v, arith_bxor(a));
	}
}

/* logical-AND: bitwise-OR ('&&' bitwise-OR)*, short-circuiting: once v
 * is known false, every further right-hand operand is parsed with
 * a->live cleared so its own side effects/errors are suppressed, per
 * this file's header comment. */
static long arith_land(struct arith *a)
{
	long v = arith_bor(a);
	for (;;) {
		int save;
		long rhs;
		skip_ws(a);
		if (!(a->p[0] == '&' && a->p[1] == '&')) return v;
		a->p += 2;
		save = a->live;
		a->live = save && (v != 0);
		rhs = arith_bor(a);
		a->live = save;
		v = (v != 0) && (rhs != 0);
	}
}

/* logical-OR: logical-AND ('||' logical-AND)*, short-circuiting the
 * same way once v is known true. */
static long arith_lor(struct arith *a)
{
	long v = arith_land(a);
	for (;;) {
		int save;
		long rhs;
		skip_ws(a);
		if (!(a->p[0] == '|' && a->p[1] == '|')) return v;
		a->p += 2;
		save = a->live;
		a->live = save && (v == 0);
		rhs = arith_land(a);
		a->live = save;
		v = (v != 0) || (rhs != 0);
	}
}

/* conditional: logical-OR ('?' assignment-expression ':' conditional)?
 * -- right-associative (a ?: chained into the false branch), and only
 * the taken branch is evaluated live, matching every other
 * short-circuit point in this file. */
static long arith_cond(struct arith *a)
{
	long c = arith_lor(a);
	skip_ws(a);
	if (*a->p == '?') {
		int save = a->live;
		long tval, fval;
		a->p++;
		a->live = save && (c != 0);
		tval = arith_assign(a);
		skip_ws(a);
		if (*a->p == ':') a->p++;
		else fail(a, WRDE_SYNTAX);
		a->live = save && (c == 0);
		fval = arith_cond(a);
		a->live = save;
		return c != 0 ? tval : fval;
	}
	return c;
}

/* assignment: ([$]NAME assign-op assignment-expression) | conditional
 * -- tries the assignment form first via a private lookahead pointer
 * so a plain conditional-expression starting with a name (e.g. "x+1",
 * or "x" with no operator at all) is never partially consumed on a
 * failed attempt. Assignment itself is right-associative, matching C's
 * "a = b = c" grouping. */
static long arith_assign(struct arith *a)
{
	const char *q = a->p;
	char name[256];
	int op;

	if (*q == '$') q++;
	if (is_namestart(*q)) {
		const char *nq = q;
		if (read_name(&nq, name)) {
			const char *oq = nq;
			while (*oq == ' ' || *oq == '\t' || *oq == '\n') oq++;
			op = match_assign_op(&oq);
			if (op) {
				long cur, rhs, v;
				a->p = oq;
				rhs = arith_assign(a);
				cur = (op == '=') ? 0 : read_var(a, name);
				v = apply_binop(a, op, cur, rhs);
				assign_var(a, name, v);
				return v;
			}
		}
	}
	return arith_cond(a);
}

/* comma: assignment-expression (',' assignment-expression)*, value of
 * the whole list is its last element -- ISO C 6.5.17, pulled in by
 * 1.1.2's "equivalent to ... Section 6.5" and not excluded by 2.6.4's
 * exception list. */
static long arith_comma(struct arith *a)
{
	long v = arith_assign(a);
	for (;;) {
		skip_ws(a);
		if (*a->p != ',') return v;
		a->p++;
		v = arith_assign(a);
	}
}

int __wordexp_arith(const char *expr, long *result, int flags)
{
	struct arith a;
	a.p = expr;
	a.flags = flags;
	a.err = 0;
	a.live = 1;

	*result = arith_comma(&a);
	if (a.err) return a.err;
	skip_ws(&a);
	if (*a.p) return WRDE_SYNTAX;	/* trailing garbage after a complete expression */
	return 0;
}
