/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Conformance tests for the in-process shell -- see test/sh-design.md
 * for why this exists at all, and src/sh/sh.h for the AST, the two
 * deliberate lexical simplifications the lexer makes, and the -1
 * "not implemented at this stage" convention __sh_exec_*() use.
 *
 * Stage 1 (lexer/parser, no execution) tests either inspect the parsed
 * AST directly or exercise the "testable on its own: parse-and-print"
 * requirement by round-tripping through __sh_print_list() and
 * reparsing. Stage 2 (execution of simple commands) tests actually
 * run processes: they re-exec this binary itself as the command,
 * matching test/misc.c's test_abort_child() pattern -- argv[1] ==
 * "--exit-child" makes main() below exit(atoi(argv[2])) immediately
 * rather than run the test suite.
 *
 * The internal entry points (__sh_parse/__sh_print_list/__sh_list_free/
 * __sh_exec_*) are not part of any installed header -- src/sh/sh.h is
 * reached with a plain relative #include, matching how the rest of
 * this test suite (test/misc.c's __spawn, test/posix-*.c's local
 * prototypes) declares internal-but-linked symbols itself rather than
 * exposing them publicly.
 *
 * Spec pages consulted (https://pubs.opengroup.org/onlinepubs/9699919799/
 * utilities/V3_chap02.html):
 *   2.2 Quoting  2.3 Token Recognition (comments: rule 9)
 *   2.7 Redirection  2.7.4 Here-Document
 *   2.9.1 Simple Commands  2.9.2 Pipelines  2.9.3 Lists
 *   2.9.4 Compound Commands (Grouping Commands)
 *   2.10.2 Shell Grammar Rules
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/sh/sh.h"

static int fails;
#define CHECK(cond) do { if (!(cond)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static struct sh_list *must_parse(const char *src)
{
	char err[256];
	struct sh_list *l = __sh_parse(src, err, sizeof err);
	if (!l) printf("  unexpected parse failure for %s: %s\n", src, err);
	CHECK(l != 0);
	return l;
}

static void must_reject(const char *src)
{
	char err[256];
	struct sh_list *l = __sh_parse(src, err, sizeof err);
	if (l) { printf("  unexpectedly accepted: %s\n", src); __sh_list_free(l); }
	CHECK(l == 0);
	CHECK(err[0] != 0); /* a diagnostic was actually written */
}

static struct sh_command *only_command(struct sh_list *l)
{
	CHECK(l->items != 0);
	if (!l->items) return 0;
	CHECK(l->items->next == 0);
	CHECK(l->items->andor != 0);
	CHECK(l->items->andor->next == 0);
	CHECK(l->items->andor->pipeline.ncommands == 1);
	return &l->items->andor->pipeline.commands[0];
}

/* ---- 2.9.1 Simple Commands ---------------------------------------------- */

static void test_simple_command_words(void)
{
	struct sh_list *l = must_parse("echo hello world");
	struct sh_command *c;
	struct sh_word *w;
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->kind == SH_CMD_SIMPLE);
	CHECK(c && c->assigns == 0);
	CHECK(c && c->redirs == 0);
	if (c) {
		w = c->words;
		CHECK(w && strcmp(w->text, "echo") == 0); w = w ? w->next : 0;
		CHECK(w && strcmp(w->text, "hello") == 0); w = w ? w->next : 0;
		CHECK(w && strcmp(w->text, "world") == 0); w = w ? w->next : 0;
		CHECK(w == 0);
	}
	__sh_list_free(l);
}

/* 2.9.1: "variable assignments specified with any of the other command
 * types... shall affect only that command's execution environment" --
 * cmd_prefix's assignment words must be recognised and kept separate
 * from the command name/arguments, and only when they precede the
 * first ordinary word (a NAME=value *after* the command name is just
 * an argument, e.g. "echo FOO=bar"). */
static void test_assignment_prefix(void)
{
	struct sh_list *l = must_parse("FOO=bar BAZ=1 echo $FOO");
	struct sh_command *c;
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->assigns && strcmp(c->assigns->text, "FOO=bar") == 0);
	CHECK(c && c->assigns && c->assigns->next && strcmp(c->assigns->next->text, "BAZ=1") == 0);
	CHECK(c && c->assigns && c->assigns->next && c->assigns->next->next == 0);
	CHECK(c && c->words && strcmp(c->words->text, "echo") == 0);
	CHECK(c && c->words && c->words->next && strcmp(c->words->next->text, "$FOO") == 0);
	__sh_list_free(l);

	l = must_parse("echo FOO=bar");
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->assigns == 0);
	CHECK(c && c->words && strcmp(c->words->text, "echo") == 0);
	CHECK(c && c->words && c->words->next && strcmp(c->words->next->text, "FOO=bar") == 0);
	__sh_list_free(l);
}

/* An all-assignment command (no cmd_word at all) is still a valid
 * simple_command per the grammar (cmd_prefix alone). */
static void test_assignment_only_command(void)
{
	struct sh_list *l = must_parse("FOO=bar");
	struct sh_command *c;
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->assigns && strcmp(c->assigns->text, "FOO=bar") == 0);
	CHECK(c && c->words == 0);
	__sh_list_free(l);
}

/* ---- 2.2 Quoting / 2.3 Token Recognition -------------------------------- */

/* An operator character loses its special meaning inside quotes, so
 * "a;b" is one word, not a command separator splitting the line. Word
 * text is kept raw (quotes intact) at this stage -- see sh.h. */
static void test_quoting_suppresses_operators(void)
{
	struct sh_list *l = must_parse("echo \"a;b\" 'c|d'");
	struct sh_command *c;
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->words && c->words->next && strcmp(c->words->next->text, "\"a;b\"") == 0);
	CHECK(c && c->words && c->words->next && c->words->next->next &&
	      strcmp(c->words->next->next->text, "'c|d'") == 0);
	__sh_list_free(l);
}

/* 2.6.3 Command Substitution / 2.6.2 Parameter Expansion: the '(' after
 * an unquoted "$(", the '{' after an unquoted "${", and the matching
 * old-form backtick pair are part of the *word*, not operator/break
 * characters -- even though '(' '{' '`' are otherwise always
 * lexer-level operators/word-enders in this implementation (see
 * sh.h). Actually running the substituted command is stage 5's job;
 * this only proves the word boundary comes out right, including
 * nested parens/braces and an embedded pipe/semicolon that must not
 * be mistaken for real operators. */
static void test_dollar_paren_and_brace_word_boundary(void)
{
	struct sh_list *l = must_parse("echo $(a | b; c) ${FOO} $((1+2))");
	struct sh_command *c;
	struct sh_word *w;
	if (!l) return;
	c = only_command(l);
	CHECK(c != 0);
	if (!c) { __sh_list_free(l); return; }
	w = c->words;
	CHECK(w && strcmp(w->text, "echo") == 0); w = w ? w->next : 0;
	CHECK(w && strcmp(w->text, "$(a | b; c)") == 0); w = w ? w->next : 0;
	CHECK(w && strcmp(w->text, "${FOO}") == 0); w = w ? w->next : 0;
	CHECK(w && strcmp(w->text, "$((1+2))") == 0); w = w ? w->next : 0;
	CHECK(w == 0);
	__sh_list_free(l);
}

static void test_backtick_word_boundary(void)
{
	struct sh_list *l = must_parse("echo `a | b; c`suffix");
	struct sh_command *c;
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->words && c->words->next && strcmp(c->words->next->text, "`a | b; c`suffix") == 0);
	CHECK(c && c->words && c->words->next && c->words->next->next == 0);
	__sh_list_free(l);
}

/* rule 9: '#' starts a comment only when it begins a new word, and it
 * runs to (not including) the next newline. */
static void test_comment(void)
{
	struct sh_list *l = must_parse("echo hi # this ; is && not | code\necho bye");
	struct sh_command *c;
	if (!l) return;
	CHECK(l->items && l->items->next && l->items->next->next == 0);
	c = &l->items->andor->pipeline.commands[0];
	CHECK(c->words && c->words->next && strcmp(c->words->next->text, "hi") == 0);
	CHECK(c->words && c->words->next && c->words->next->next == 0);
	c = &l->items->next->andor->pipeline.commands[0];
	CHECK(c->words && strcmp(c->words->text, "echo") == 0);
	__sh_list_free(l);

	l = must_parse("echo abc#def");
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->words && c->words->next && strcmp(c->words->next->text, "abc#def") == 0);
	__sh_list_free(l);
}

static void test_empty_and_comment_only(void)
{
	struct sh_list *l = must_parse("");
	CHECK(l && l->items == 0);
	__sh_list_free(l);

	l = must_parse("   \n\n  ");
	CHECK(l && l->items == 0);
	__sh_list_free(l);

	l = must_parse("# just a comment\n");
	CHECK(l && l->items == 0);
	__sh_list_free(l);
}

/* ---- 2.9.2 Pipelines ----------------------------------------------------- */

static void test_pipeline(void)
{
	struct sh_list *l = must_parse("a | b | c");
	struct sh_pipeline *pl;
	if (!l) return;
	pl = &l->items->andor->pipeline;
	CHECK(pl->ncommands == 3);
	CHECK(pl->bang == 0);
	CHECK(strcmp(pl->commands[0].words->text, "a") == 0);
	CHECK(strcmp(pl->commands[1].words->text, "b") == 0);
	CHECK(strcmp(pl->commands[2].words->text, "c") == 0);
	__sh_list_free(l);
}

static void test_pipeline_bang(void)
{
	struct sh_list *l = must_parse("! false");
	struct sh_pipeline *pl;
	if (!l) return;
	pl = &l->items->andor->pipeline;
	CHECK(pl->bang == 1);
	CHECK(pl->ncommands == 1);
	CHECK(strcmp(pl->commands[0].words->text, "false") == 0);
	__sh_list_free(l);
}

/* ---- 2.9.3 Lists ----------------------------------------------------------
 * "&&" and "||" are left-associative and have equal precedence; a
 * pipeline not preceded by one is unconditional (SH_AO_NONE here). */
static void test_andor(void)
{
	struct sh_list *l = must_parse("a && b || c");
	struct sh_andor *a;
	if (!l) return;
	a = l->items->andor;
	CHECK(a && a->op == SH_AO_NONE && strcmp(a->pipeline.commands[0].words->text, "a") == 0);
	a = a ? a->next : 0;
	CHECK(a && a->op == SH_AO_AND && strcmp(a->pipeline.commands[0].words->text, "b") == 0);
	a = a ? a->next : 0;
	CHECK(a && a->op == SH_AO_OR && strcmp(a->pipeline.commands[0].words->text, "c") == 0);
	CHECK(a && a->next == 0);
	__sh_list_free(l);
}

/* ';' and '&' are both separators; '&' additionally marks the
 * preceding and-or list for asynchronous execution.  A trailing
 * separator (or none) both leave the final item's sep distinguishable
 * (SH_SEP_END here means "no separator was present"). */
static void test_list_separators(void)
{
	struct sh_list *l = must_parse("a; b & c");
	struct sh_list_item *it;
	if (!l) return;
	it = l->items;
	CHECK(it && it->sep == SH_SEP_SEQ && strcmp(it->andor->pipeline.commands[0].words->text, "a") == 0);
	it = it ? it->next : 0;
	CHECK(it && it->sep == SH_SEP_AMP && strcmp(it->andor->pipeline.commands[0].words->text, "b") == 0);
	it = it ? it->next : 0;
	CHECK(it && it->sep == SH_SEP_END && strcmp(it->andor->pipeline.commands[0].words->text, "c") == 0);
	CHECK(it && it->next == 0);
	__sh_list_free(l);
}

/* ---- 2.9.4 Compound Commands (Grouping Commands) -------------------------- */

static void test_subshell(void)
{
	struct sh_list *l = must_parse("(a; b)");
	struct sh_command *c;
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->kind == SH_CMD_SUBSHELL);
	CHECK(c && c->body && c->body->items && c->body->items->next && c->body->items->next->next == 0);
	CHECK(c && c->body && strcmp(c->body->items->andor->pipeline.commands[0].words->text, "a") == 0);
	CHECK(c && c->body && strcmp(c->body->items->next->andor->pipeline.commands[0].words->text, "b") == 0);
	__sh_list_free(l);
}

static void test_brace_group(void)
{
	struct sh_list *l = must_parse("{ a; b; }");
	struct sh_command *c;
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->kind == SH_CMD_BRACE);
	CHECK(c && c->body && c->body->items && c->body->items->next && c->body->items->next->next == 0);
	__sh_list_free(l);
}

static void test_subshell_nested_and_redirected(void)
{
	struct sh_list *l = must_parse("(echo hi) > out");
	struct sh_command *c;
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->kind == SH_CMD_SUBSHELL);
	CHECK(c && c->redirs && c->redirs->op == SH_R_GREAT && strcmp(c->redirs->word, "out") == 0);
	__sh_list_free(l);
}

/* ---- 2.7 Redirection ------------------------------------------------------ */

static void test_redirections(void)
{
	struct sh_list *l = must_parse("cmd > out 2>&1 < in 3<&4 >> app");
	struct sh_command *c;
	struct sh_redir *r;
	if (!l) return;
	c = only_command(l);
	CHECK(c != 0);
	if (!c) { __sh_list_free(l); return; }
	r = c->redirs;
	CHECK(r && r->op == SH_R_GREAT && r->fd == -1 && strcmp(r->word, "out") == 0); r = r ? r->next : 0;
	CHECK(r && r->op == SH_R_GREATAND && r->fd == 2 && strcmp(r->word, "1") == 0); r = r ? r->next : 0;
	CHECK(r && r->op == SH_R_LESS && r->fd == -1 && strcmp(r->word, "in") == 0); r = r ? r->next : 0;
	CHECK(r && r->op == SH_R_LESSAND && r->fd == 3 && strcmp(r->word, "4") == 0); r = r ? r->next : 0;
	CHECK(r && r->op == SH_R_DGREAT && r->fd == -1 && strcmp(r->word, "app") == 0); r = r ? r->next : 0;
	CHECK(r == 0);
	/* the io_number/redirect target words never became command args */
	CHECK(c->words && c->words->next == 0 && strcmp(c->words->text, "cmd") == 0);
	__sh_list_free(l);
}

/* ---- 2.7.4 Here-Document --------------------------------------------------- */

static void test_heredoc_basic(void)
{
	struct sh_list *l = must_parse("cat <<EOF\nhello\nworld\nEOF\n");
	struct sh_command *c;
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->redirs && c->redirs->op == SH_R_DLESS);
	CHECK(c && c->redirs && strcmp(c->redirs->word, "EOF") == 0);
	CHECK(c && c->redirs && !c->redirs->heredoc_quoted);
	CHECK(c && c->redirs && c->redirs->heredoc && strcmp(c->redirs->heredoc, "hello\nworld\n") == 0);
	__sh_list_free(l);
}

/* "<<-" strips leading <tab> characters from the body *and* the
 * delimiter line. */
static void test_heredoc_dash_strips_tabs(void)
{
	struct sh_list *l = must_parse("cat <<-END\n\t\thello\n\tEND\n");
	struct sh_command *c;
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->redirs && c->redirs->op == SH_R_DLESSDASH);
	CHECK(c && c->redirs && c->redirs->heredoc && strcmp(c->redirs->heredoc, "hello\n") == 0);
	__sh_list_free(l);
}

/* A quoted delimiter ("shall be quoted" -> no expansions in the body)
 * is still matched against the *unquoted* terminator line, per 2.7.4's
 * "quote removal is performed on the delimiter". */
static void test_heredoc_quoted_delimiter(void)
{
	struct sh_list *l = must_parse("cat <<'EOF'\n$x literal\nEOF\n");
	struct sh_command *c;
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->redirs && c->redirs->heredoc_quoted == 1);
	CHECK(c && c->redirs && c->redirs->heredoc && strcmp(c->redirs->heredoc, "$x literal\n") == 0);
	__sh_list_free(l);
}

static void test_heredoc_two_on_one_line(void)
{
	struct sh_list *l = must_parse("cat <<A <<B\nfirst\nA\nsecond\nB\n");
	struct sh_command *c;
	if (!l) return;
	c = only_command(l);
	CHECK(c && c->redirs && c->redirs->heredoc && strcmp(c->redirs->heredoc, "first\n") == 0);
	CHECK(c && c->redirs && c->redirs->next && c->redirs->next->heredoc &&
	      strcmp(c->redirs->next->heredoc, "second\n") == 0);
	__sh_list_free(l);
}

/* ---- 2.10.2 Shell Grammar Rules: malformed input must be rejected -------- */

static void test_rejects_malformed(void)
{
	must_reject("(a; b");                 /* unterminated subshell */
	must_reject("a)");                    /* stray ')' */
	must_reject("{ a; b");                /* unterminated brace group */
	must_reject("a &&");                  /* '&&' with no right operand */
	must_reject("a |");                   /* '|' with no right side */
	must_reject("| a");                   /* pipeline with no left side */
	must_reject("echo \"unterminated");   /* unterminated double quote */
	must_reject("echo 'unterminated");    /* unterminated single quote */
	must_reject("echo \\");               /* trailing backslash, nothing to escape */
	must_reject("cat <<EOF\nno terminator here\n"); /* here-doc delimiter never seen */
	must_reject(">");                     /* redirection operator with no target word */
	must_reject(";");                     /* separator with nothing before it */
}

/* ---- parse-and-print round trip: stage 1's other testability requirement */

static void check_roundtrip(const char *src)
{
	struct sh_list *l1, *l2;
	char buf1[1024], buf2[1024];
	FILE *f1, *f2;

	l1 = must_parse(src);
	if (!l1) return;
	f1 = fmemopen(buf1, sizeof buf1, "w");
	CHECK(f1 != 0);
	if (f1) { __sh_print_list(f1, l1); fclose(f1); }
	__sh_list_free(l1);
	if (!f1) return;

	l2 = must_parse(buf1);
	if (!l2) return;
	f2 = fmemopen(buf2, sizeof buf2, "w");
	CHECK(f2 != 0);
	if (f2) { __sh_print_list(f2, l2); fclose(f2); }
	__sh_list_free(l2);
	if (!f2) return;

	if (strcmp(buf1, buf2) != 0)
		printf("  roundtrip mismatch for %s:\n    1: %s    2: %s\n", src, buf1, buf2);
	CHECK(strcmp(buf1, buf2) == 0);
}

static void test_roundtrip(void)
{
	check_roundtrip("echo hello world");
	check_roundtrip("FOO=bar echo $FOO");
	check_roundtrip("a | b | c");
	check_roundtrip("! a | b");
	check_roundtrip("a && b || c; d & e");
	check_roundtrip("(a; b) | { c; }");
	check_roundtrip("cmd > out 2>&1 < in");
	check_roundtrip("cat <<EOF\nhello\nworld\nEOF\n");
	check_roundtrip("cat <<-END\n\thi\n\tEND\n");
	check_roundtrip("echo \"a;b\" 'c|d'");
	check_roundtrip("echo $(a | b; c) ${FOO} $((1+2))");
	check_roundtrip("echo `a | b; c`suffix");
}

/* ---- stage 2: execution of simple commands -----------------------------
 *
 * These re-exec this very binary (matching test/misc.c's
 * test_abort_child() pattern) as the command a parsed sh_list runs, so
 * a real __find_program()+__spawn()+waitpid() round trip happens --
 * argv[1] == "--exit-child" makes main() below exit(atoi(argv[2]))
 * immediately instead of running the test suite. `self` is wrapped in
 * single quotes in the source text since a Windows path's backslashes
 * must stay completely literal (single-quote quoting, not double, so
 * wordexp()'s expansion pass can't try to interpret one as an escape).
 */
static int run(const char *src, int *status)
{
	struct sh_list *l = must_parse(src);
	int rc;
	if (!l) return -1;
	rc = __sh_exec_list(l, status);
	__sh_list_free(l);
	return rc;
}

static void test_exec_simple_command_status(const char *self)
{
	char src[512];
	int status;
	snprintf(src, sizeof src, "'%s' --exit-child 7", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 7);

	snprintf(src, sizeof src, "'%s' --exit-child 0", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
}

/* command-not-found matches system()'s documented exit-127 clause
 * (src/stdlib/system.c), which stage 5 must not regress. */
static void test_exec_command_not_found(void)
{
	int status;
	CHECK(run("this-program-genuinely-does-not-exist-xyz", &status) == 0);
	CHECK(status == 127);
}

/* 2.9.2: "!" inverts the pipeline's exit status. */
static void test_exec_bang_negation(const char *self)
{
	char src[512];
	int status;
	snprintf(src, sizeof src, "! '%s' --exit-child 0", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 1);

	snprintf(src, sizeof src, "! '%s' --exit-child 5", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
}

/* 2.9.3: "&&"/"||" short-circuit on the previous command's status. */
static void test_exec_andor_short_circuit(const char *self)
{
	char src[512];
	int status;

	snprintf(src, sizeof src, "'%s' --exit-child 0 && '%s' --exit-child 9", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 9); /* left succeeded, right ran and its status wins */

	snprintf(src, sizeof src, "'%s' --exit-child 3 && '%s' --exit-child 9", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 3); /* left failed, right never ran -- status is still the left's */

	snprintf(src, sizeof src, "'%s' --exit-child 0 || '%s' --exit-child 9", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0); /* left succeeded, right never ran */
}

/* 2.9.1: an assignment-only simple command (no cmd_word) affects the
 * *current* execution environment, not a child's. */
static void test_exec_assignment_only_affects_shell_env(void)
{
	int status;
	unsetenv("SH_TEST_ASSIGN_ONLY");
	CHECK(run("SH_TEST_ASSIGN_ONLY=hello", &status) == 0);
	CHECK(status == 0);
	{
		const char *v = getenv("SH_TEST_ASSIGN_ONLY");
		CHECK(v && strcmp(v, "hello") == 0);
	}
	unsetenv("SH_TEST_ASSIGN_ONLY");
}

/* An assignment prefixed to a command with a cmd_word is scoped to
 * that command's own execution environment (build_child_envp() in
 * exec.c copies environ rather than mutating it) -- the shell's own
 * environment must come out unchanged, which is exactly what
 * test/sh-design.md means by "must never clobber the caller's ...
 * environ". */
static void test_exec_assignment_prefix_does_not_leak(const char *self)
{
	char src[512];
	int status;
	unsetenv("SH_TEST_ASSIGN_SCOPED");
	snprintf(src, sizeof src, "SH_TEST_ASSIGN_SCOPED=childonly '%s' --exit-child 0", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
	CHECK(getenv("SH_TEST_ASSIGN_SCOPED") == 0);
}

/* Proves word realization actually goes through wordexp() (rather than
 * some from-scratch reimplementation): an unquoted $VAR in a command
 * word is expanded before argv reaches __spawn(). */
static void test_exec_reuses_wordexp_param_expansion(const char *self)
{
	char src[512];
	int status;
	setenv("SH_TEST_EXIT_CODE", "42", 1);
	snprintf(src, sizeof src, "'%s' --exit-child $SH_TEST_EXIT_CODE", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 42);
	unsetenv("SH_TEST_EXIT_CODE");
}

/* Constructs this stage do not implement are reported as -1 ("cannot
 * execute this AST node yet"), never silently misexecuted -- see
 * sh.h's __sh_exec_*() comment. */
static void test_exec_reports_unimplemented_constructs(void)
{
	int status;
	CHECK(run("true | false", &status) == -1);       /* stage 3: pipelines */
	CHECK(run("echo hi > /tmp/x", &status) == -1);    /* stage 3: redirection */
	CHECK(run("(echo hi)", &status) == -1);           /* stage 4: subshells */
	CHECK(run("{ echo hi; }", &status) == -1);        /* stage 4: brace groups */
}

int main(int argc, char **argv)
{
	if (argc > 2 && strcmp(argv[1], "--exit-child") == 0)
		return atoi(argv[2]);

	test_simple_command_words();
	test_assignment_prefix();
	test_assignment_only_command();

	test_quoting_suppresses_operators();
	test_dollar_paren_and_brace_word_boundary();
	test_backtick_word_boundary();
	test_comment();
	test_empty_and_comment_only();

	test_pipeline();
	test_pipeline_bang();

	test_andor();
	test_list_separators();

	test_subshell();
	test_brace_group();
	test_subshell_nested_and_redirected();

	test_redirections();

	test_heredoc_basic();
	test_heredoc_dash_strips_tabs();
	test_heredoc_quoted_delimiter();
	test_heredoc_two_on_one_line();

	test_rejects_malformed();

	test_roundtrip();

	test_exec_simple_command_status(argv[0]);
	test_exec_command_not_found();
	test_exec_bang_negation(argv[0]);
	test_exec_andor_short_circuit(argv[0]);
	test_exec_assignment_only_affects_shell_env();
	test_exec_assignment_prefix_does_not_leak(argv[0]);
	test_exec_reuses_wordexp_param_expansion(argv[0]);
	test_exec_reports_unimplemented_constructs();

	if (fails) { printf("sh: failures: %d\n", fails); return 1; }
	printf("sh: all ok (stage 2: lexer + parser + execution of simple commands -- see test/sh-design.md)\n");
	return 0;
}
