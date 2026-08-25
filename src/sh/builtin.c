/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Stage 6a: a real built-in *dispatcher*, and the built-in utilities
 * the compound-command grammar (stages 6b onward) needs before it is
 * usable at all.
 *
 * ---- Why this file exists ---------------------------------------------
 *
 * Until this stage src/sh/exec.c had exactly one built-in, `cd`, matched
 * with a raw strcmp() on the *unexpanded* first word, and its own
 * comment said so explicitly: "this builtin exists to make stage 4's
 * subshell/brace tests exercisable, not to be a general-purpose builtin
 * dispatcher".  Two things are wrong with growing more built-ins that
 * way, and both are fixed here rather than replicated:
 *
 *  - **It matched the wrong string.**  XCU 2.9.1 ("Command Search and
 *    Execution") searches for the command name *after* the word
 *    expansions of 2.9.1 step 1 have run, so `c=cd; $c /tmp` is a `cd`
 *    and `'cd' /tmp` is one too.  Matching raw source text answers a
 *    different question.  This file is therefore consulted from
 *    exec.c's spawn path with the already-expanded argv, which is the
 *    string 2.9.1 actually names.
 *  - **There was nowhere to record what a built-in *is*.**  2.14
 *    distinguishes *special* built-ins (whose failure exits a
 *    non-interactive shell, 2.8.1) from regular ones, and 2.12 makes
 *    "the working directory" and the shell's own environment part of
 *    the shell execution environment -- so some built-ins must run in
 *    this process and some must not when they are one stage of a
 *    multi-command pipeline (2.12: "each command of a multi-command
 *    pipeline is in a subshell environment").  A strcmp() chain has no
 *    room for either fact; a table has a column for each.
 *
 * ---- Why these particular utilities, and why they cannot be programs --
 *
 * `test`/`[`, `:`, `true`, `false` and `exit` are not built in here for
 * speed.  On a POSIX system `test`, `true` and `false` are genuine
 * external utilities and sh/main.c's comment said as much -- which is
 * exactly why it did *not* refuse them and why they came back as an
 * honest exit 127.  This platform has no /bin at all: there is no
 * `test.exe`, no `true.exe`, nothing for __find_program() to find.  So
 * "let PATH lookup fail honestly" degenerates into "every conditional
 * a real script writes is 127", and a conditional that always fails the
 * same way is worse than one that is refused, which is the property
 * test/sh-design.md's "Placement and gates" is about.  `:` and `exit`
 * are 2.14 special built-ins and could never be programs anywhere.
 *
 * Counted across 100887 lines of five real autoconf `configure` scripts
 * (keywords at statement position), `test` is used 5488 times -- 229x
 * more often than the `[ ... ]` spelling, which appears 24 times.  Both
 * are the same utility (see this file's `[` handling), but the ratio is
 * why `test` is not an afterthought to a bracket implementation.
 *
 * ---- What `test` implements ------------------------------------------
 *
 * XCU test(1p).  Its OPERANDS section fixes the primaries, and its
 * EXTENDED DESCRIPTION fixes something subtler that a naive
 * "tokenise and evaluate" implementation gets wrong: *the meaning of an
 * argument depends on how many arguments there are*.  The standard
 * gives explicit rules for 0, 1, 2, 3 and 4 arguments and says results
 * are unspecified beyond that, which is why `test "(" = ")"` is a
 * string comparison (3 arguments: "$2 is a binary primary") and not a
 * parenthesised group, and why `test ! -n` is a 2-argument negation of
 * the string "-n" rather than a malformed unary primary.  eval_argc()
 * below implements those five cases literally, in the standard's own
 * order, and only falls through to the recursive-descent grammar for
 * the >4 case -- where the standard's XSI paragraph does describe a
 * precedence/associativity evaluation, and where every shell in
 * practice provides one.
 *
 * Exit status is test(1p)'s: 0 for a true expression, 1 for false or
 * null, ">1 An error occurred" -- 2 here, with a diagnostic, for a
 * malformed expression or a non-integer operand of an arithmetic
 * primary.  A silent "false" for a malformed expression would be the
 * same undiagnosable wrongness this project keeps refusing.
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "libc.h"
#include "sh.h"

/* ==== test / [ (XCU test(1p)) =========================================== */

/* test(1p) EXIT STATUS: "0 expression evaluated to true", "1 expression
 * evaluated to false or expression was missing", ">1 An error
 * occurred". */
#define T_TRUE  0
#define T_FALSE 1
#define T_ERR   2

struct texpr {
	char **v;      /* the arguments being evaluated, v[0] is the first */
	int n;         /* how many there are */
	int i;         /* cursor for the >4-argument grammar */
	int err;       /* set once a diagnostic has been issued */
};

static void terr(struct texpr *t, const char *msg, const char *arg)
{
	if (t->err) return;
	t->err = 1;
	if (arg) fprintf(stderr, "test: %s: %s\n", arg, msg);
	else fprintf(stderr, "test: %s\n", msg);
}

/* An integer operand of -eq/-ne/-lt/-le/-gt/-ge.  test(1p) calls these
 * operands "integers" with no allowance for anything else, so anything
 * strtol() does not consume in full is an error (status >1), not a
 * silently-zero comparison.  Surrounding blanks are tolerated because
 * field splitting routinely produces them and every historical
 * implementation accepts them. */
static int to_int(struct texpr *t, const char *s, long *out)
{
	char *end;
	const char *p = s;
	while (*p == ' ' || *p == '\t' || *p == '\n') p++;
	if (!*p) { terr(t, "integer expression expected", s); return -1; }
	*out = strtol(p, &end, 10);
	if (end == p) { terr(t, "integer expression expected", s); return -1; }
	while (*end == ' ' || *end == '\t' || *end == '\n') end++;
	if (*end) { terr(t, "integer expression expected", s); return -1; }
	return 0;
}

static int is_binop(const char *s)
{
	return !strcmp(s, "=") || !strcmp(s, "!=") ||
	       !strcmp(s, "-eq") || !strcmp(s, "-ne") ||
	       !strcmp(s, "-lt") || !strcmp(s, "-le") ||
	       !strcmp(s, "-gt") || !strcmp(s, "-ge");
}

/* The unary primaries of test(1p)'s OPERANDS section.  -a is
 * deliberately absent: as a *unary* primary it is a non-standard
 * synonym for -e that some shells provide, and providing it would make
 * `test ! -a foo` ambiguous with the -a binary primary that the same
 * page does specify. */
static int is_unop(const char *s)
{
	if (s[0] != '-' || s[1] == 0 || s[2] != 0) return 0;
	return strchr("bcdefghLnprSstuwxz", s[1]) != 0;
}

static int do_unary(struct texpr *t, const char *op, const char *arg)
{
	struct stat st;

	switch (op[1]) {
	case 'n': return arg[0] != 0 ? T_TRUE : T_FALSE;
	case 'z': return arg[0] == 0 ? T_TRUE : T_FALSE;
	/* "True if file descriptor number file_descriptor is open and is
	 * associated with a terminal.  False if file_descriptor is not a
	 * valid file descriptor number" -- so a non-numeric operand is
	 * false, not an error. */
	case 't': {
		char *end;
		long fd = strtol(arg, &end, 10);
		if (end == arg || *end || fd < 0 || fd > 0x7fffffff) return T_FALSE;
		return isatty((int)fd) ? T_TRUE : T_FALSE;
	}
	case 'r': return access(arg, R_OK) == 0 ? T_TRUE : T_FALSE;
	case 'w': return access(arg, W_OK) == 0 ? T_TRUE : T_FALSE;
	case 'x': return access(arg, X_OK) == 0 ? T_TRUE : T_FALSE;
	/* "If the final component of pathname is a symbolic link, that
	 * symbolic link is not followed" -- -h and -L are the only two
	 * primaries that use lstat() rather than stat(). */
	case 'h': case 'L':
		if (lstat(arg, &st) < 0) return T_FALSE;
		return S_ISLNK(st.st_mode) ? T_TRUE : T_FALSE;
	default: break;
	}

	if (stat(arg, &st) < 0) return T_FALSE;
	switch (op[1]) {
	case 'e': return T_TRUE;                                     /* resolves at all */
	case 'f': return S_ISREG(st.st_mode) ? T_TRUE : T_FALSE;
	case 'd': return S_ISDIR(st.st_mode) ? T_TRUE : T_FALSE;
	case 'b': return S_ISBLK(st.st_mode) ? T_TRUE : T_FALSE;
	case 'c': return S_ISCHR(st.st_mode) ? T_TRUE : T_FALSE;
	case 'p': return S_ISFIFO(st.st_mode) ? T_TRUE : T_FALSE;
	case 'S': return S_ISSOCK(st.st_mode) ? T_TRUE : T_FALSE;
	case 's': return st.st_size > 0 ? T_TRUE : T_FALSE;
	case 'g': return (st.st_mode & S_ISGID) ? T_TRUE : T_FALSE;
	case 'u': return (st.st_mode & S_ISUID) ? T_TRUE : T_FALSE;
	default: break;
	}
	terr(t, "unknown unary operator", op);
	return T_ERR;
}

static int do_binary(struct texpr *t, const char *a, const char *op, const char *b)
{
	long x, y;
	if (!strcmp(op, "=")) return strcmp(a, b) == 0 ? T_TRUE : T_FALSE;
	if (!strcmp(op, "!=")) return strcmp(a, b) != 0 ? T_TRUE : T_FALSE;
	if (to_int(t, a, &x) || to_int(t, b, &y)) return T_ERR;
	if (!strcmp(op, "-eq")) return x == y ? T_TRUE : T_FALSE;
	if (!strcmp(op, "-ne")) return x != y ? T_TRUE : T_FALSE;
	if (!strcmp(op, "-lt")) return x < y ? T_TRUE : T_FALSE;
	if (!strcmp(op, "-le")) return x <= y ? T_TRUE : T_FALSE;
	if (!strcmp(op, "-gt")) return x > y ? T_TRUE : T_FALSE;
	if (!strcmp(op, "-ge")) return x >= y ? T_TRUE : T_FALSE;
	terr(t, "unknown binary operator", op);
	return T_ERR;
}

/* ---- the >4-argument grammar ----------------------------------------
 *
 * test(1p): ">4 arguments: The results are unspecified", followed by
 * the XSI paragraph that does specify one -- "combinations of primaries
 * and operators shall be evaluated using the precedence and
 * associativity rules described previously.  In addition, the string
 * comparison binary primaries '=' and '!=' shall have a higher
 * precedence than any unary primary."  -a is left-associative and binds
 * tighter than -o, which is also left-associative.
 *
 * That last sentence is why t_primary() tests for a binary primary
 * *before* it tests for a unary one: with the checks the other way
 * round, `test -n = -n -o x` would consume "-n" as a unary primary and
 * never see the "=" it is the left operand of. */
static int t_oexpr(struct texpr *t);

static int t_primary(struct texpr *t)
{
	const char *tok;

	if (t->i >= t->n) { terr(t, "argument expected", 0); return T_ERR; }
	tok = t->v[t->i];

	if (!strcmp(tok, "(")) {
		int r;
		t->i++;
		r = t_oexpr(t);
		if (t->err) return T_ERR;
		if (t->i >= t->n || strcmp(t->v[t->i], ")")) { terr(t, "')' expected", 0); return T_ERR; }
		t->i++;
		return r;
	}
	/* higher precedence than any unary primary -- see above */
	if (t->i + 2 < t->n && is_binop(t->v[t->i + 1])) {
		int r = do_binary(t, t->v[t->i], t->v[t->i + 1], t->v[t->i + 2]);
		t->i += 3;
		return r;
	}
	if (is_unop(tok) && t->i + 1 < t->n) {
		int r = do_unary(t, tok, t->v[t->i + 1]);
		t->i += 2;
		return r;
	}
	/* "string: True if the string string is not the null string" */
	t->i++;
	return tok[0] != 0 ? T_TRUE : T_FALSE;
}

static int t_nexpr(struct texpr *t)
{
	if (t->i < t->n && !strcmp(t->v[t->i], "!")) {
		int r;
		t->i++;
		r = t_nexpr(t);
		if (r == T_ERR || t->err) return T_ERR;
		return r == T_TRUE ? T_FALSE : T_TRUE;
	}
	return t_primary(t);
}

static int t_aexpr(struct texpr *t)
{
	int r = t_nexpr(t);
	while (!t->err && t->i < t->n && !strcmp(t->v[t->i], "-a")) {
		int rhs;
		t->i++;
		rhs = t_nexpr(t);
		/* Evaluated, not short-circuited: an error in either operand
		 * of -a is still an error (status >1), and skipping the right
		 * operand would hide a malformed one behind a false left. */
		if (r == T_ERR || rhs == T_ERR) r = T_ERR;
		else r = (r == T_TRUE && rhs == T_TRUE) ? T_TRUE : T_FALSE;
	}
	return t->err ? T_ERR : r;
}

static int t_oexpr(struct texpr *t)
{
	int r = t_aexpr(t);
	while (!t->err && t->i < t->n && !strcmp(t->v[t->i], "-o")) {
		int rhs;
		t->i++;
		rhs = t_aexpr(t);
		if (r == T_ERR || rhs == T_ERR) r = T_ERR;
		else r = (r == T_TRUE || rhs == T_TRUE) ? T_TRUE : T_FALSE;
	}
	return t->err ? T_ERR : r;
}

/* test(1p) EXTENDED DESCRIPTION, taken literally and in its own order:
 * "The algorithm for determining the precedence of the operators and
 * the return value that shall be generated is based on the number of
 * arguments presented to test." */
static int eval_argc(struct texpr *t)
{
	char **v = t->v;
	int n = t->n;

	switch (n) {
	case 0:
		/* "0 arguments: Exit false (1)." */
		return T_FALSE;
	case 1:
		/* "1 argument: Exit true (0) if $1 is not null; otherwise,
		 * exit false." */
		return v[0][0] != 0 ? T_TRUE : T_FALSE;
	case 2:
		/* "If $1 is '!', exit true if $2 is null, false if $2 is not
		 * null." -- note this is a *string* test of $2, not a
		 * negated evaluation of it, so `test ! -n` is false: it is
		 * the negation of "-n" being a non-null string, not a
		 * malformed unary primary and not "not (-n)". */
		if (!strcmp(v[0], "!")) return v[1][0] == 0 ? T_TRUE : T_FALSE;
		if (is_unop(v[0])) return do_unary(t, v[0], v[1]);
		terr(t, "unary operator expected", v[0]);
		return T_ERR;
	case 3:
		/* "If $2 is a binary primary, perform the binary test of $1
		 * and $3." -- checked first, which is what makes
		 * `test "(" = ")"` a string comparison. */
		if (is_binop(v[1])) return do_binary(t, v[0], v[1], v[2]);
		if (!strcmp(v[0], "!")) {
			struct texpr sub = *t;
			int r;
			sub.v = v + 1; sub.n = 2; sub.i = 0;
			r = eval_argc(&sub);
			t->err = sub.err;
			if (r == T_ERR) return T_ERR;
			return r == T_TRUE ? T_FALSE : T_TRUE;
		}
		/* XSI: "If $1 is '(' and $3 is ')', perform the unary test
		 * of $2" -- i.e. the one-argument (non-null string) test. */
		if (!strcmp(v[0], "(") && !strcmp(v[2], ")")) return v[1][0] != 0 ? T_TRUE : T_FALSE;
		terr(t, "binary operator expected", v[1]);
		return T_ERR;
	case 4:
		/* "If $1 is '!', negate the three-argument test of $2, $3,
		 * and $4." */
		if (!strcmp(v[0], "!")) {
			struct texpr sub = *t;
			int r;
			sub.v = v + 1; sub.n = 3; sub.i = 0;
			r = eval_argc(&sub);
			t->err = sub.err;
			if (r == T_ERR) return T_ERR;
			return r == T_TRUE ? T_FALSE : T_TRUE;
		}
		/* XSI: "If $1 is '(' and $4 is ')', perform the two-argument
		 * test of $2 and $3." */
		if (!strcmp(v[0], "(") && !strcmp(v[3], ")")) {
			struct texpr sub = *t;
			int r;
			sub.v = v + 1; sub.n = 2; sub.i = 0;
			r = eval_argc(&sub);
			t->err = sub.err;
			return r;
		}
		/* fall through to the grammar: "Otherwise, the results are
		 * unspecified", and the XSI precedence rules are a more
		 * useful answer than a diagnostic for e.g.
		 * "-f a -o -f b". */
		break;
	default:
		break;
	}

	t->i = 0;
	{
		int r = t_oexpr(t);
		if (!t->err && t->i != t->n) { terr(t, "unexpected argument", t->v[t->i]); return T_ERR; }
		return t->err ? T_ERR : r;
	}
}

static int bi_test(struct sh_builtin_ctx *ctx)
{
	struct texpr t;
	int n = ctx->argc - 1;

	/* "In the second form of the utility, where the utility name used
	 * is [ rather than test, the application shall ensure that the
	 * closing square bracket is a separate argument."  Its absence is
	 * an error, and the bracket itself is "not ... counted in this
	 * algorithm". */
	if (!strcmp(ctx->argv[0], "[")) {
		if (n < 1 || strcmp(ctx->argv[ctx->argc - 1], "]")) {
			fprintf(stderr, "[: missing `]'\n");
			ctx->status = T_ERR;
			return 0;
		}
		n--;
	}

	t.v = ctx->argv + 1;
	t.n = n;
	t.i = 0;
	t.err = 0;
	ctx->status = eval_argc(&t);
	return 0;
}

/* ==== the trivial four ================================================== */

/* XCU 2.14: ": [argument...] -- This utility shall only expand command
 * arguments.  It is used when a command is needed, as in the then
 * condition of an if command, but nothing is to be done by the
 * command.  EXIT STATUS: Zero."  The expansion has already happened by
 * the time this runs (exec.c calls the dispatcher with expanded argv),
 * which is exactly the specified behaviour. */
static int bi_colon(struct sh_builtin_ctx *ctx)
{
	ctx->status = 0;
	return 0;
}

/* XCU true(1p) / false(1p): "shall return with exit code zero" /
 * "shall return with a non-zero exit code".  Regular utilities, not
 * 2.14 special built-ins -- they are built in here only because this
 * platform has no true.exe/false.exe for __find_program() to find (see
 * this file's header). */
static int bi_true(struct sh_builtin_ctx *ctx)
{
	ctx->status = 0;
	return 0;
}

static int bi_false(struct sh_builtin_ctx *ctx)
{
	ctx->status = 1;
	return 0;
}

/* XCU 2.14: "exit [n] -- ... shall cause the shell to exit with the
 * exit status specified by the unsigned decimal integer n.  If n is
 * specified, but its value is not between 0 and 255 inclusively, the
 * exit status is undefined.  ... If n is not specified, the exit status
 * shall be that of the last command executed."
 *
 * `env_mutate == 0` is exec.c's flag for "this command's effect on the
 * shell execution environment is going to be discarded anyway" -- a
 * stage of a multi-command pipeline, which 2.12 places in a subshell
 * environment.  `exit` there exits *that* subshell, so the requested
 * status becomes the stage's status and no shell-wide unwind is
 * started.  The same is true of `( exit 3 )`, but that is handled one
 * level up, by exec_group() consuming the pending exit, because a
 * subshell's body is a whole list rather than a single command. */
static int bi_exit(struct sh_builtin_ctx *ctx)
{
	int st;

	if (ctx->argc > 1) {
		char *end;
		long v = strtol(ctx->argv[1], &end, 10);
		if (end == ctx->argv[1] || *end) {
			/* 2.8.1: "an error in a special built-in utility ...
			 * shall cause a non-interactive shell to exit"; the
			 * status is implementation-defined and 2 is what
			 * bash/dash use for a numeric-argument error here. */
			fprintf(stderr, "exit: %s: numeric argument required\n", ctx->argv[1]);
			st = 2;
		} else {
			st = (int)(v & 0xff);
		}
	} else {
		st = ctx->last_status;
	}
	ctx->status = st;
	if (ctx->env_mutate) __sh_flow_exit(st);
	return 0;
}

/* XCU cd(1p), and XCU 2.12: "Working directory as set by cd" is part of
 * the shell execution environment, so this can only ever run in the
 * shell's own process -- there is no cd.exe on any platform, and there
 * could not usefully be one.  Moved here from src/sh/exec.c, which
 * implemented it inline against the *unexpanded* first word and said in
 * its own comment that it was "not to be a general-purpose builtin
 * dispatcher"; this file is that dispatcher, and cd is now dispatched
 * on the expanded command name like every other built-in (XCU 2.9.1).
 *
 * Still deliberately not a complete cd(1p): no CDPATH search, no -L/-P
 * logical/physical distinction, no "cd -" to OLDPWD.  PWD and OLDPWD
 * are updated so a later $PWD read is not silently stale. */
static int bi_cd(struct sh_builtin_ctx *ctx)
{
	const char *target = ctx->argc > 1 ? ctx->argv[1] : getenv("HOME");
	char *oldcwd, *newcwd;

	if (!target || !*target) {
		/* cd(1p): "If ... HOME is unset or null, the results are
		 * unspecified" -- failing the command is a conforming
		 * choice. */
		ctx->status = 1;
		return 0;
	}
	oldcwd = getcwd(0, 0);
	if (chdir(target) < 0) {
		__free(oldcwd);
		ctx->status = 1;
		return 0;
	}
	newcwd = getcwd(0, 0);
	if (oldcwd) setenv("OLDPWD", oldcwd, 1);
	if (newcwd) setenv("PWD", newcwd, 1);
	__free(oldcwd);
	__free(newcwd);
	ctx->status = 0;
	return 0;
}

/* ==== set / shift: the positional parameters (XCU 2.5.1) =============== */

/* set(1p) with no options and no arguments: "set shall write the names
 * and values of all shell variables in the collation sequence of the
 * current locale.  Each name shall start on a separate line, using the
 * format: "%s=%s\n" ... The value string shall be written with
 * appropriate quoting ... The output shall be suitable for reinput to
 * the shell".
 *
 * "Suitable for reinput" is the part that a naive `printf("%s\n", e)`
 * over environ gets wrong the moment a value contains a space, a '$' or
 * a quote -- and gets wrong *silently*, producing output that looks
 * right and means something else when fed back.  So the value is
 * single-quoted (XCU 2.2.2: "[e]nclosing characters in single-quotes
 * shall preserve the literal value of each character within the
 * single-quotes"), with the one character that cannot appear inside
 * single-quotes -- a single-quote -- written as the standard
 * '\''  splice: close, escape one, reopen.
 *
 * The deviation that remains, stated rather than hidden: this shell's
 * only variable store is the real `environ` (see src/sh/exec.c), so
 * what is listed is the environment, not a separate set of unexported
 * shell variables, and there is no collation-order sort. */
static void write_quoted(const char *v)
{
	fputc('\'', stdout);
	for (; *v; v++) {
		if (*v == '\'') fputs("'\\''", stdout);
		else fputc(*v, stdout);
	}
	fputc('\'', stdout);
}

static void set_list_variables(void)
{
	extern char **environ;
	char **e;

	for (e = environ; e && *e; e++) {
		const char *eq = strchr(*e, '=');
		if (!eq) { fputs(*e, stdout); fputc('\n', stdout); continue; }
		fwrite(*e, 1, (size_t)(eq - *e), stdout);
		fputc('=', stdout);
		write_quoted(eq + 1);
		fputc('\n', stdout);
	}
}

/* set(1p): "The remaining arguments shall be assigned in order to the
 * positional parameters.  The special parameter '#' shall be set to
 * reflect the number of positional parameters.  All positional
 * parameters shall be unset before any new values are assigned", and
 * "[t]he command set -- without argument shall unset all positional
 * parameters and set the special parameter '#' to zero."
 *
 * Options are not implemented, and that is a refusal rather than a
 * silent no-op: `set -e` that did nothing would change the meaning of
 * every subsequent failure in the script without the script being able
 * to tell, which is exactly what sh/main.c's refusal list exists to
 * prevent.  set(1p)'s EXIT STATUS makes ">0  An invalid option was
 * specified, or an error occurred" the right shape for saying so.
 *
 * `env_effect` is 0 in the table below, not 1, for the same reason
 * `exit`'s is: the no-operand form *writes to standard output*, which a
 * pipeline stage must still do ("set | ..." is an ordinary idiom), so
 * the utility has to run either way and decides for itself which half
 * of its behaviour a subshell environment discards. */
static int bi_set(struct sh_builtin_ctx *ctx)
{
	int first = 1;

	if (ctx->argc == 1) {
		set_list_variables();
		ctx->status = 0;
		return 0;
	}
	if (strcmp(ctx->argv[1], "--") == 0) {
		first = 2;
	} else if (ctx->argv[1][0] == '-' || ctx->argv[1][0] == '+') {
		fprintf(stderr, "set: %s: options are not implemented -- see "
		                "test/sh-design.md\n", ctx->argv[1]);
		ctx->status = 2;
		return 0;
	}
	/* 2.12 puts a multi-command pipeline's stages in a subshell
	 * environment, and this process is not one: renumbering the real
	 * shell's parameters from a stage would leak out of a subshell
	 * that is supposed to be discarded.  Not doing it is
	 * indistinguishable from doing it in a discarded subshell. */
	if (!ctx->env_mutate) { ctx->status = 0; return 0; }
	if (__sh_params_replace(ctx->argv + first, ctx->argc - first) < 0) {
		fprintf(stderr, "set: out of memory\n");
		ctx->status = 2;
		return 0;
	}
	ctx->status = 0;
	return 0;
}

/* shift(1p): "The value n shall be an unsigned decimal integer less
 * than or equal to the value of the special parameter '#'.  If n is not
 * given, it shall be assumed to be 1.  If n is 0, the positional and
 * special parameters are not changed."  EXIT STATUS: "[i]f the n
 * operand is invalid or is greater than "$#" ... a non-zero exit status
 * shall be returned."
 *
 * "Unsigned decimal integer" is taken literally: a leading '-' or '+',
 * or any trailing text, is invalid rather than something to salvage,
 * because `shift $x` with an $x that expanded to nothing or to a word
 * is precisely the case where guessing produces a wrong-but-plausible
 * argument list further down the script. */
static int bi_shift(struct sh_builtin_ctx *ctx)
{
	long n = 1;

	if (ctx->argc > 2) {
		fprintf(stderr, "shift: too many operands\n");
		ctx->status = 2;
		return 0;
	}
	if (ctx->argc == 2) {
		const char *a = ctx->argv[1];
		char *end;
		if (!*a || !(*a >= '0' && *a <= '9')) {
			fprintf(stderr, "shift: %s: not an unsigned decimal integer\n", a);
			ctx->status = 2;
			return 0;
		}
		n = strtol(a, &end, 10);
		if (*end) {
			fprintf(stderr, "shift: %s: not an unsigned decimal integer\n", a);
			ctx->status = 2;
			return 0;
		}
	}
	if (n > __sh_param_count()) {
		fprintf(stderr, "shift: can only shift %d positional parameter%s\n",
			__sh_param_count(), __sh_param_count() == 1 ? "" : "s");
		ctx->status = 2;
		return 0;
	}
	/* Same subshell reasoning as bi_set() above. */
	if (!ctx->env_mutate) { ctx->status = 0; return 0; }
	if (__sh_params_shift((int)n) < 0) { ctx->status = 2; return 0; }
	ctx->status = 0;
	return 0;
}

/* ==== the dispatcher ==================================================== */

/* `special` is XCU 2.14's distinction, recorded because 2.8.1 hangs
 * consequences off it; `env_effect` says the utility changes something
 * 2.12 lists as part of the shell execution environment, so exec.c must
 * not run it in-process when the invocation's effect is scoped to a
 * subshell environment that is about to be discarded. */
static const struct sh_builtin builtins[] = {
	{ ":",     1, 0, bi_colon },
	/* `exit`'s env_effect is 0 on purpose: its effect in a subshell
	 * environment *is* the exit status, which bi_exit() produces
	 * either way -- so unlike `cd` it must still run when env_mutate
	 * is 0, and it consults ctx->env_mutate itself to decide whether
	 * to start a shell-wide unwind. */
	{ "exit",  1, 0, bi_exit },
	/* 2.14 special built-ins.  `env_effect` 0 for both: see bi_set()'s
	 * header comment -- each has an output half that must run in a
	 * pipeline stage and a mutating half that must not, so each
	 * consults ctx->env_mutate itself rather than being skipped
	 * wholesale the way `cd` is. */
	{ "set",   1, 0, bi_set },
	{ "shift", 1, 0, bi_shift },
	{ "cd",    0, 1, bi_cd },
	{ "test",  0, 0, bi_test },
	{ "[",     0, 0, bi_test },
	{ "true",  0, 0, bi_true },
	{ "false", 0, 0, bi_false },
	{ 0, 0, 0, 0 }
};

const struct sh_builtin *__sh_builtin_lookup(const char *name)
{
	size_t i;
	for (i = 0; builtins[i].name; i++)
		if (strcmp(builtins[i].name, name) == 0) return &builtins[i];
	return 0;
}
