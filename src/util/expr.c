/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * expr(1p): evaluate an expression given as separate argv operands and
 * print the result.  Checked against
 * https://pubs.opengroup.org/onlinepubs/9699919799/utilities/expr.html.
 *
 * GRAMMAR (that page's OPERANDS table, "in order of decreasing
 * precedence, with equal-precedence operators grouped"; all binary
 * operators are left-associative):
 *
 *   expr1 '|' expr2
 *   expr1 '&' expr2
 *   expr1 { '=', '>', '>=', '<', '<=', '!=' } expr2
 *   expr1 { '+', '-' } expr2
 *   expr1 { '*', '/', '%' } expr2
 *   expr1 ':' expr2
 *   '(' expr1 ')'
 *
 * Each argv operand after argv[0] is already one token -- the shell (or
 * whatever invoked this utility) has already done the word-splitting
 * expr's own grammar needs, so this file's parser walks argv directly,
 * the same "recursive descent over an argv cursor" shape
 * src/util/test.c's t_oexpr()/t_aexpr()/t_nexpr() already use for
 * test(1p)'s not-unrelated `!`/`-a`/`-o` grammar (test(1p)'s own
 * comment explains why that shape fits an already-tokenized argv well).
 * expr(1p)'s grammar is a genuinely different operator set and
 * precedence table, though -- and unlike test(1p), a value here is
 * evaluated exactly once, so this parser builds and evaluates in one
 * pass (no separate AST) rather than parsing into a tree first.
 *
 * NUMERIC-VS-STRING COERCION: "An argument ... that consists only of an
 * optional unary minus followed by digits is a candidate for treatment
 * as an integer if it is used as the left argument to the '|' operator
 * or as either argument to any of the ... '&' '=' '>' '>=' '<' '<=' '!='
 * '+' '-' '*' '/' '%' [operators]. Otherwise, the argument is treated as
 * a string."  Implemented literally by is_num_candidate() below: for
 * '+'/'-'/'*'/'/'/'%' both operands must be candidates or the whole
 * expression is invalid (exit 2, "non-numeric argument"); for the
 * comparison operators, arithmetic comparison is used only when BOTH
 * operands are candidates, otherwise a plain byte/strcmp comparison is
 * used (this library's only locale is "C", src/misc/locale.c, so there
 * is no stronger collation to fall back to).
 *
 * '|' and '&' do NOT return a boolean the way shell/C's do: "'|': ...
 * evaluation of expr1 if it is neither null nor zero; otherwise ...
 * evaluation of expr2." / "'&': ... evaluation of expr1 if neither
 * expression evaluates to null or zero; otherwise ... zero." --
 * null_or_zero() below is exactly the "null or zero" test both operators
 * (and the final EXIT STATUS check) share: the empty string, or a
 * numeric candidate whose value is 0.
 *
 * ':' MATCH OPERATOR: "compare the string resulting from ... expr1 with
 * the regular expression pattern ... expr2. ... Basic Regular
 * Expressions ... except that all patterns are anchored to the
 * beginning of the string ... if the pattern contains at least one
 * ... subexpression \\(...\\), the string matched by the back-reference
 * expression \\1 shall be returned [or the null string if \\1 did not
 * match]; otherwise ... the number of characters matched ... or ... '0'
 * [if there was no match]."  Both operands of ':' are always treated as
 * strings, regardless of whether either looks numeric -- the table
 * above deliberately does not list ':' among the candidate-triggering
 * operators.  src/regex/regex.c's regcomp()/regexec() (default BRE,
 * REG_EXTENDED not passed) implement the pattern language; "anchored to
 * the beginning" is implemented by requiring pmatch[0].rm_so == 0 on a
 * successful match rather than by rewriting the pattern, since
 * regexec() here (like grep) finds the leftmost match anywhere in the
 * string unless told otherwise.
 *
 * EXIT STATUS (this file gets this exactly right, per the page's own
 * EXIT STATUS section): "0 The expression evaluates to neither null nor
 * zero. 1 The expression evaluates to null or zero. 2 The expression is
 * invalid. >2 An error occurred."  Every parse/evaluation problem below
 * (syntax error, non-numeric arithmetic operand, division by zero, a
 * malformed ':' pattern) is reported as 2, matching "the expression is
 * invalid" -- this file has no case that reaches >2, since none of its
 * own operations can fail for a reason other than the expression itself
 * being invalid (no file I/O, no allocation of a size an attacker
 * controls in a way that could plausibly exhaust memory on a real
 * expr(1p) invocation).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include "util.h"

struct expr_ctx {
	char **v;
	size_t n;
	size_t i;
	int err;
};

static void xerr(struct expr_ctx *c, const char *msg) __attribute__((nonnull(1, 2)));
static void xerr(struct expr_ctx *c, const char *msg)
{
	if (c->err) return;
	c->err = 1;
	__util_diagf("expr: %s\n", msg);
}

static int is_num_candidate(const char *s) __attribute__((nonnull(1), __pure__));
static int is_num_candidate(const char *s)
{
	const char *p = s;
	if (*p == '-') p++;
	if (!*p) return 0;
	for (; *p; p++) if (*p < '0' || *p > '9') return 0;
	return 1;
}

/* "null or zero": the shared test '|', '&' and the final EXIT STATUS
 * decision all use, per this file's own header comment. */
static int null_or_zero(const char *v) __attribute__((nonnull(1), __pure__));
static int null_or_zero(const char *v)
{
	if (!*v) return 1;
	return is_num_candidate(v) && strtol(v, NULL, 10) == 0;
}

static const char *peek(struct expr_ctx *c) __attribute__((nonnull(1)));
static const char *peek(struct expr_ctx *c)
{
	return c->i < c->n ? c->v[c->i] : NULL;
}

withtok(heap_allocated)
static char *dupstr(const char *s) __attribute__((nonnull(1)));
withtok(heap_allocated)
static char *dupstr(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = malloc(n);
	if (!p) { __util_diagf("expr: out of memory\n"); exit(2); }
	memcpy(p, s, n);
	return p;
}

withtok(heap_allocated)
static char *numstr(long n)
{
	char buf[32];
	snprintf(buf, sizeof buf, "%ld", n);
	return dupstr(buf);
}

withtok(heap_allocated)
static char *parse_or(struct expr_ctx *c) __attribute__((nonnull(1)));
withtok(heap_allocated)
static char *parse_and(struct expr_ctx *c) __attribute__((nonnull(1)));
withtok(heap_allocated)
static char *parse_cmp(struct expr_ctx *c) __attribute__((nonnull(1)));
withtok(heap_allocated)
static char *parse_add(struct expr_ctx *c) __attribute__((nonnull(1)));
withtok(heap_allocated)
static char *parse_mul(struct expr_ctx *c) __attribute__((nonnull(1)));
withtok(heap_allocated)
static char *parse_match(struct expr_ctx *c) __attribute__((nonnull(1)));
withtok(heap_allocated)
static char *parse_primary(struct expr_ctx *c) __attribute__((nonnull(1)));

static int is_cmp_op(const char *s) __attribute__((nonnull(1), __pure__));
static int is_cmp_op(const char *s)
{
	return !strcmp(s, "=") || !strcmp(s, ">") || !strcmp(s, ">=") ||
	       !strcmp(s, "<") || !strcmp(s, "<=") || !strcmp(s, "!=");
}

/* p is required: every path dereferences it (strcmp against p is
 * unconditional).  c is left unmarked -- diagnostics go through xerr(),
 * which states its own contract. */
withtok(heap_allocated)
static char *do_arith(struct expr_ctx *c, char *a consume(heap_allocated), const char *op,
	char *b consume(heap_allocated)) __attribute__((nonnull(3)));
withtok(heap_allocated)
static char *do_arith(struct expr_ctx *c, char *a consume(heap_allocated), const char *op,
	char *b consume(heap_allocated)) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	long x, y, r;
	char *result;
	if (c->err) { result = dupstr(""); goto done; }
	if (!is_num_candidate(a) || !is_num_candidate(b)) {
		xerr(c, "non-numeric argument");
		result = dupstr("");
		goto done;
	}
	x = strtol(a, NULL, 10);
	y = strtol(b, NULL, 10);
	if (!strcmp(op, "+")) r = x + y;
	else if (!strcmp(op, "-")) r = x - y;
	else if (!strcmp(op, "*")) r = x * y;
	else {
		if (y == 0) { xerr(c, "division by zero"); result = dupstr(""); goto done; }
		r = !strcmp(op, "/") ? x / y : x % y;
	}
	result = numstr(r);
done:
	free(a);
	free(b);
	return result;
}

withtok(heap_allocated)
static char *do_cmp(struct expr_ctx *c, char *a consume(heap_allocated), const char *op,
	char *b consume(heap_allocated)) __attribute__((nonnull(3)));
withtok(heap_allocated)
static char *do_cmp(struct expr_ctx *c, char *a consume(heap_allocated), const char *op,
	char *b consume(heap_allocated)) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	int r;
	const char *value;
	char *result;
	if (c->err) { result = dupstr(""); goto done; }
	if (is_num_candidate(a) && is_num_candidate(b)) {
		long x = strtol(a, NULL, 10), y = strtol(b, NULL, 10);
		r = x < y ? -1 : x > y ? 1 : 0;
	} else {
		r = strcmp(a, b);
		r = r < 0 ? -1 : r > 0 ? 1 : 0;
	}
	if (!strcmp(op, "=")) value = r == 0 ? "1" : "0";
	else if (!strcmp(op, "!=")) value = r != 0 ? "1" : "0";
	else if (!strcmp(op, "<")) value = r < 0 ? "1" : "0";
	else if (!strcmp(op, "<=")) value = r <= 0 ? "1" : "0";
	else if (!strcmp(op, ">")) value = r > 0 ? "1" : "0";
	else value = r >= 0 ? "1" : "0"; /* ">=" */
	result = dupstr(value);
done:
	free(a);
	free(b);
	return result;
}

withtok(heap_allocated)
static char *do_match(struct expr_ctx *c, char *a consume(heap_allocated),
	char *pat consume(heap_allocated)) __attribute__((nonnull(2, 3)));
withtok(heap_allocated)
static char *do_match(struct expr_ctx *c, char *a consume(heap_allocated),
	char *pat consume(heap_allocated))
{
	regex_t re;
	regmatch_t pm[2];
	int rc, matched;
	char *result;

	if (c->err) { result = dupstr(""); goto done; }
	rc = regcomp(&re, pat, 0);
	if (rc) {
		xerr(c, "invalid regular expression");
		result = dupstr("");
		goto done;
	}
	rc = regexec(&re, a, 2, pm, 0);
	matched = rc == 0 && pm[0].rm_so == 0;
	if (re.re_nsub >= 1) {
		if (matched && pm[1].rm_so >= 0) {
			regoff_t len = pm[1].rm_eo - pm[1].rm_so;
			result = malloc((size_t)len + 1);
			if (!result) { xerr(c, "out of memory"); result = dupstr(""); }
			else {
				memcpy(result, a + pm[1].rm_so, (size_t)len);
				result[len] = 0;
			}
		} else {
			result = dupstr("");
		}
	} else {
		result = matched ? numstr(pm[0].rm_eo - pm[0].rm_so) : dupstr("0");
	}
	regfree(&re);
done:
	free(a);
	free(pat);
	return result;
}

// NOLINTNEXTLINE(misc-no-recursion) -- recursive descent mirrors nested expr grouping and is depth-bounded by argc
withtok(heap_allocated)
static char *parse_primary(struct expr_ctx *c)
{
	const char *tok = peek(c);
	if (!tok) { xerr(c, "syntax error: unexpected end of expression"); return dupstr(""); }
	if (!strcmp(tok, "(")) {
		char *v;
		c->i++;
		v = parse_or(c);
		tok = peek(c);
		if (!tok || strcmp(tok, ")")) { xerr(c, "syntax error: expected ')'"); return v; } // NOLINT(bugprone-suspicious-string-compare) -- nonzero intentionally detects a missing/mismatched ')'
		c->i++;
		return v;
	}
	if (!strcmp(tok, ")")) { xerr(c, "syntax error: unexpected ')'"); return dupstr(""); }
	c->i++;
	return dupstr(tok);
}

// NOLINTNEXTLINE(misc-no-recursion) -- recursive descent mirrors nested expr grouping and is depth-bounded by argc
withtok(heap_allocated)
static char *parse_match(struct expr_ctx *c)
{
	char *v = parse_primary(c);
	while (peek(c) && !strcmp(peek(c), ":")) {
		char *rhs;
		c->i++;
		rhs = parse_primary(c);
		v = do_match(c, v, rhs);
	}
	return v;
}

// NOLINTNEXTLINE(misc-no-recursion) -- recursive descent mirrors nested expr grouping and is depth-bounded by argc
withtok(heap_allocated)
static char *parse_mul(struct expr_ctx *c)
{
	char *v = parse_match(c);
	const char *tok;
	while ((tok = peek(c)) && (!strcmp(tok, "*") || !strcmp(tok, "/") || !strcmp(tok, "%"))) {
		char *rhs;
		c->i++;
		rhs = parse_match(c);
		v = do_arith(c, v, tok, rhs);
	}
	return v;
}

// NOLINTNEXTLINE(misc-no-recursion) -- recursive descent mirrors nested expr grouping and is depth-bounded by argc
withtok(heap_allocated)
static char *parse_add(struct expr_ctx *c)
{
	char *v = parse_mul(c);
	const char *tok;
	while ((tok = peek(c)) && (!strcmp(tok, "+") || !strcmp(tok, "-"))) {
		char *rhs;
		c->i++;
		rhs = parse_mul(c);
		v = do_arith(c, v, tok, rhs);
	}
	return v;
}

// NOLINTNEXTLINE(misc-no-recursion) -- recursive descent mirrors nested expr grouping and is depth-bounded by argc
withtok(heap_allocated)
static char *parse_cmp(struct expr_ctx *c)
{
	char *v = parse_add(c);
	while (peek(c) && is_cmp_op(peek(c))) {
		const char *op = peek(c);
		char *rhs;
		c->i++;
		rhs = parse_add(c);
		v = do_cmp(c, v, op, rhs);
	}
	return v;
}

// NOLINTNEXTLINE(misc-no-recursion) -- recursive descent mirrors nested expr grouping and is depth-bounded by argc
withtok(heap_allocated)
static char *parse_and(struct expr_ctx *c)
{
	char *v = parse_cmp(c);
	while (peek(c) && !strcmp(peek(c), "&")) {
		char *rhs;
		c->i++;
		rhs = parse_cmp(c);
		if (null_or_zero(v) || null_or_zero(rhs)) {
			free(v);
			free(rhs);
			v = dupstr("0");
		} else {
			free(rhs);
		}
	}
	return v;
}

// NOLINTNEXTLINE(misc-no-recursion) -- recursive descent mirrors nested expr grouping and is depth-bounded by argc
withtok(heap_allocated)
static char *parse_or(struct expr_ctx *c)
{
	char *v = parse_and(c);
	while (peek(c) && !strcmp(peek(c), "|")) {
		char *rhs;
		c->i++;
		rhs = parse_and(c);
		if (null_or_zero(v)) {
			free(v);
			v = rhs;
		} else {
			free(rhs);
		}
	}
	return v;
}

int __util_expr_main(int argc, char **argv)
{
	struct expr_ctx c;
	char *result;
	int status;

	if (argc < 2) {
		__util_diagf("expr: missing operand\n");
		return 2;
	}

	c.v = argv + 1;
	c.n = (size_t)(argc - 1);
	c.i = 0;
	c.err = 0;

	result = parse_or(&c);
	if (!c.err && c.i != c.n) xerr(&c, "syntax error: unexpected argument");
	if (c.err) { free(result); return 2; }

	printf("%s\n", result);
	status = null_or_zero(result) ? 1 : 0;
	free(result);
	return status;
}
