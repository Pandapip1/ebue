/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Conformance tests for the in-process shell *engine* -- see
 * test/sh-design.md for why this exists at all, and src/sh/sh.h for the
 * AST, the two deliberate lexical simplifications the lexer makes, and
 * the -1 "not implemented at this stage" convention __sh_exec_*() use.
 *
 * Named sh-engine.c, not sh.c: every test/%.c builds to obj/test/%.exe,
 * and the real shell binary (sh/main.c -> obj/sh/sh.exe) owns the name
 * sh.exe.  Two different sh.exe's would be indistinguishable in a build
 * log, and CI's real-Windows legs download every test executable into
 * one flat directory -- where one would silently shadow the other and
 * the leg would still go green.  The name also says what this file
 * actually covers: the engine linked out of libc.a, driven in-process
 * through __sh_parse()/__sh_exec_*(), never a second image.  The black-
 * box tests of the *program* -- argument handling, exit status, the
 * diagnostics -- live in test/sh-main.c and spawn obj/sh/sh.exe for
 * real.
 *
 * Stage 1 (lexer/parser, no execution) tests either inspect the parsed
 * AST directly or exercise the "testable on its own: parse-and-print"
 * requirement by round-tripping through __sh_print_list() and
 * reparsing. Stage 2 (execution of simple commands) and stage 3
 * (redirections and pipelines) tests actually run processes: they
 * re-exec this binary itself as the command(s), matching test/misc.c's
 * test_abort_child() pattern -- argv[1] selects a role (see
 * child_role() below) that makes main() act as that role instead of
 * running the test suite. "--exit-child N" is stage 2's; stage 3 adds
 * a handful more (--produce, --cat, --stdin-eq, --produce-both,
 * --fd-open) so redirection and pipeline tests do not depend on any
 * external program (no /bin/cat, /bin/true, ... on this platform).
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
 *   2.7 Redirection  2.7.1 Redirecting Input  2.7.2 Redirecting Output
 *   2.7.3 Appending Redirected Output  2.7.4 Here-Document
 *   2.7.5/2.7.6 Duplicating an Input/Output File Descriptor
 *   2.7.7 Open File Descriptors for Reading and Writing
 *   2.8.1 Consequences of Shell Errors
 *   2.9.1 Simple Commands  2.9.2 Pipelines  2.9.3 Lists
 *   2.9.4 Compound Commands (Grouping Commands)
 *   2.10.2 Shell Grammar Rules
 *   2.2.2 Single-Quotes  2.2.3 Double-Quotes
 *   2.6.3 Command Substitution  2.6.5 Field Splitting
 *   2.12 Shell Execution Environment
 *   2.14 Special Built-In Utilities
 * and, for stage 6a's built-ins, the utility pages themselves:
 *   utilities/test.html (OPERANDS, EXTENDED DESCRIPTION, EXIT STATUS)
 *   utilities/true.html  utilities/false.html  utilities/cd.html
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
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

/* Constructs no stage yet implements are reported as -1 ("cannot
 * execute this AST node yet"), never silently misexecuted -- see
 * sh.h's __sh_exec_*() comment.
 *
 * This test used to assert exactly the opposite of what it asserts now.
 * Command substitution in a redirection target word and in an unquoted
 * here-document body were stage 4's named, tested "not yet"; stage 5
 * implements them, so those three -1s became results, and they are
 * checked as results by test_exec_cmdsub_in_redirections() below. What
 * is left here is the convention itself, which is still live and still
 * needs a construct that genuinely reaches it: two directly adjacent
 * compound-command stages in one pipeline, which src/sh/exec.c refuses
 * up front rather than deadlock without a fork(). */
static void test_exec_reports_unimplemented_constructs(const char *self)
{
	char src[512];
	int status;

	snprintf(src, sizeof src, "('%s' --produce a) | { '%s' --cat; }", self, self);
	CHECK(run(src, &status) == -1);

	snprintf(src, sizeof src, "{ '%s' --produce a; } | ('%s' --cat)", self, self);
	CHECK(run(src, &status) == -1);
}

/* ---- stage 3: redirections and pipelines --------------------------------
 *
 * These reuse run()/must_parse() from stage 2 above, plus a handful of
 * new self-exec roles (child_role() below) that stand in for the
 * external `cat`/`true`/`echo` a Unix test suite could otherwise
 * assume: this platform has none of those as standalone programs, and
 * test/sh-design.md's whole point is that this shell must not depend
 * on one either.
 */

/* A fresh, empty temp file in the current directory -- the same
 * mkstemp()-in-cwd convention test/stdio.c's make_tmp() and
 * test/stdlib.c use. Returns a malloc'd path (already created) the
 * caller must remove()+free(), or NULL on failure. */
static char *make_tmp(void)
{
	char tmpl[] = "shtstXXXXXX";
	int fd = mkstemp(tmpl);
	if (fd < 0) return 0;
	close(fd);
	return strdup(tmpl);
}

/* Reads a whole (small) file into a malloc'd, NUL-terminated buffer,
 * or returns NULL if it cannot be opened. Used to inspect what a
 * redirected command actually wrote. */
static char *slurp(const char *path)
{
	FILE *f = fopen(path, "rb");
	char *buf;
	long len;
	if (!f) return 0;
	if (fseek(f, 0, SEEK_END)) { fclose(f); return 0; }
	len = ftell(f);
	if (len < 0 || fseek(f, 0, SEEK_SET)) { fclose(f); return 0; }
	buf = malloc((size_t)len + 1);
	if (!buf) { fclose(f); return 0; }
	if (len && fread(buf, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(buf); return 0; }
	buf[len] = 0;
	fclose(f);
	return buf;
}

/* Whether a *spawned child* can actually observe any std-descriptor
 * redirection its parent process performed -- true on real Windows and
 * under Wine (this file's normal targets), but not under
 * tools/asan-build.sh's native ASan build: that harness's
 * RtlCreateUserProcess stub (fuzz/ntstubs.c) is a real host
 * fork()+execve() of a real host binary that never reads
 * pp->StandardInput/Output/Error at all, so a spawned child there
 * always gets the *harness's original* real host fd 0/1/2, no matter
 * what this process's own (correctly rewired) descriptor table says at
 * the moment of the call. This applies uniformly -- an open()'d file
 * (backed by that stub's in-memory-only vnode filesystem, with no real
 * host fd for a separate process to inherit either way), a
 * close()d/dup2()'d descriptor, and even a pipe (whose ends *are* real
 * host fds, but still never reach the child, precisely because nothing
 * ever copies them onto its fd 0/1/2) are all equally invisible to
 * anything this process spawns there. Detected once, at runtime, by
 * actually trying the simplest case (file redirection) rather than
 * assumed from a compile-time macro -- the same "find the environment
 * gap by checking, then skip only what it affects, with a note"
 * discipline test/posix-termios.c's /dev/tty checks already use, so
 * this keeps working unattended if that stub's process model ever
 * changes. Every test that needs a spawned child to observe some
 * redirection its parent made -- file-based, here-document, pipeline,
 * or close/dup -- gates on this; test_exec_redir_dup_closed_fd_fails()
 * is the one exception, since a redirection error keeps the command
 * from ever spawning at all and so does not depend on this. */
static int file_redir_supported(const char *self)
{
	static int cached = -1;
	char src[512], *tmp, *got;
	int status;

	if (cached >= 0) return cached;
	cached = 0;
	tmp = make_tmp();
	if (!tmp) return cached;
	snprintf(src, sizeof src, "'%s' --produce probe > %s", self, tmp);
	if (run(src, &status) == 0 && status == 0) {
		got = slurp(tmp);
		if (got && strcmp(got, "probe\n") == 0) cached = 1;
		free(got);
	}
	remove(tmp);
	free(tmp);
	if (!cached)
		printf("  note: a spawned child cannot see this process's file-based"
		       " redirections in this environment (native ASan stub?) --"
		       " skipping file/here-document redirection checks\n");
	return cached;
}

static void test_exec_redir_output_creates_and_truncates(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512], *tmp = make_tmp(), *got;
	CHECK(tmp != 0);
	if (!tmp) return;

	/* '>' creates (2.7.2) ... */
	snprintf(src, sizeof src, "'%s' --produce first > %s", self, tmp);
	{
		int status;
		CHECK(run(src, &status) == 0);
		CHECK(status == 0);
	}
	got = slurp(tmp);
	CHECK(got && strcmp(got, "first\n") == 0);
	free(got);

	/* ... and each subsequent '>' truncates rather than appending. */
	snprintf(src, sizeof src, "'%s' --produce second > %s", self, tmp);
	{
		int status;
		CHECK(run(src, &status) == 0);
		CHECK(status == 0);
	}
	got = slurp(tmp);
	CHECK(got && strcmp(got, "second\n") == 0);
	free(got);

	remove(tmp);
	free(tmp);
}

/* 2.7.3: '>>' appends instead of truncating. */
static void test_exec_redir_append(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512], *tmp = make_tmp(), *got;
	int status;
	CHECK(tmp != 0);
	if (!tmp) return;

	snprintf(src, sizeof src, "'%s' --produce one >> %s", self, tmp);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
	snprintf(src, sizeof src, "'%s' --produce two >> %s", self, tmp);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);

	got = slurp(tmp);
	CHECK(got && strcmp(got, "one\ntwo\n") == 0);
	free(got);
	remove(tmp);
	free(tmp);
}

/* '>|' is only documented (2.7.2) to differ from '>' when the shell's
 * noclobber option (set -C) is set; this shell has no `set` builtin at
 * all yet (test/sh-design.md's scope), so nothing ever refuses a plain
 * '>' in the first place and '>|' has nothing extra to override --
 * see apply_one_redir()'s SH_R_CLOBBER case in src/sh/exec.c for the
 * same point made where the behavior actually lives. This just checks
 * '>|' still *works* like '>', not that it is indistinguishable in
 * every hypothetical future (a `set -C` builtin would only need to
 * change the SH_R_GREAT case). */
static void test_exec_redir_clobber_like_great(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512], *tmp = make_tmp(), *got;
	int status;
	CHECK(tmp != 0);
	if (!tmp) return;
	snprintf(src, sizeof src, "'%s' --produce hi >| %s", self, tmp);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
	got = slurp(tmp);
	CHECK(got && strcmp(got, "hi\n") == 0);
	free(got);
	remove(tmp);
	free(tmp);
}

/* 2.7.1: '<' feeds a file to the command's stdin. */
static void test_exec_redir_input(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512], *tmp = make_tmp();
	int status;
	FILE *f;
	CHECK(tmp != 0);
	if (!tmp) return;
	f = fopen(tmp, "wb");
	CHECK(f != 0);
	if (f) { fputs("seed\n", f); fclose(f); }

	snprintf(src, sizeof src, "'%s' --stdin-eq seed < %s", self, tmp);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
	remove(tmp);
	free(tmp);
}

/* 2.7.7: '<>' opens for both reading and writing, without truncating
 * (unlike '<' + '>' or '>' alone) -- the seeded content must still be
 * there, and readable, afterward. */
static void test_exec_redir_lessgreat_no_truncate(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512], *tmp = make_tmp(), *got;
	int status;
	FILE *f;
	CHECK(tmp != 0);
	if (!tmp) return;
	f = fopen(tmp, "wb");
	CHECK(f != 0);
	if (f) { fputs("seed\n", f); fclose(f); }

	snprintf(src, sizeof src, "'%s' --stdin-eq seed <> %s", self, tmp);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);

	got = slurp(tmp);
	CHECK(got && strcmp(got, "seed\n") == 0); /* untouched -- no O_TRUNC */
	free(got);
	remove(tmp);
	free(tmp);
}

/* 2.7: "the order of evaluation is from beginning to end" -- so which
 * of two redirections targeting the *same* descriptor number wins is
 * whichever is written last. */
static void test_exec_redir_order_last_wins(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512], *t1 = make_tmp(), *t2 = make_tmp(), *got;
	int status;
	CHECK(t1 != 0 && t2 != 0);
	if (!t1 || !t2) { free(t1); free(t2); return; }

	snprintf(src, sizeof src, "'%s' --produce hi > %s > %s", self, t1, t2);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);

	got = slurp(t1);
	CHECK(got && got[0] == 0); /* opened (and truncated) but never written to */
	free(got);
	got = slurp(t2);
	CHECK(got && strcmp(got, "hi\n") == 0);
	free(got);

	remove(t1); free(t1);
	remove(t2); free(t2);
}

/* 2.7.6, and the classic ordering trap: "cmd >file 2>&1" sends both
 * streams to file (fd 2 is duplicated from fd 1 *after* fd 1 already
 * points at file), while "cmd 2>&1 >file" does not (fd 2 is duplicated
 * from the *old* fd 1 first, and only then does fd 1 move to file) --
 * this is exactly the ordering apply_redirs()/apply_one_redir() in
 * src/sh/exec.c process left to right, never as a batch. */
static void test_exec_redir_dup_output_ordering(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512], *t1 = make_tmp(), *t2 = make_tmp(), *got;
	int status;
	CHECK(t1 != 0 && t2 != 0);
	if (!t1 || !t2) { free(t1); free(t2); return; }

	/* stdout then stderr both land in t1: fd 2 follows fd 1 to it. */
	snprintf(src, sizeof src, "'%s' --produce-both OUT ERR > %s 2>&1", self, t1);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
	got = slurp(t1);
	CHECK(got && strcmp(got, "OUT\nERR\n") == 0);
	free(got);

	/* fd 2 is bound to the *old* stdout first; only stdout then moves
	 * to t2, so t2 must contain OUT but never ERR. */
	snprintf(src, sizeof src, "'%s' --produce-both OUT ERR 2>&1 > %s", self, t2);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
	got = slurp(t2);
	CHECK(got && strcmp(got, "OUT\n") == 0);
	free(got);

	remove(t1); free(t1);
	remove(t2); free(t2);
}

/* 2.7.6: "<&-"/">&-" close the descriptor instead of duplicating it. */
static void test_exec_redir_close(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512];
	int status;

	snprintf(src, sizeof src, "'%s' --fd-open 0", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 1); /* control: fd 0 is normally open */

	snprintf(src, sizeof src, "'%s' --fd-open 0 <&-", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0); /* closed */

	snprintf(src, sizeof src, "'%s' --fd-open 1 >&-", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0); /* closed */
}

/* 2.7.6: duplicating a closed/nonexistent descriptor is a redirection
 * error -- 2.8.1 says that "shall not exit" a non-interactive shell
 * for an ordinary utility, it just fails that one command. */
static void test_exec_redir_dup_closed_fd_fails(const char *self)
{
	char src[512];
	int status;
	snprintf(src, sizeof src, "'%s' --exit-child 0 <&9", self);
	CHECK(run(src, &status) == 0);  /* not "-1 unimplemented" */
	CHECK(status != 0);             /* the command failed to even start */
}

/* 2.8.1's "shall not exit" applies just as much to an outright open()
 * failure (no such directory) as to a bad fd -- and, in a pipeline,
 * only *that* stage is affected; the rest runs normally. */
static void test_exec_redir_open_failure_nonabort(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512];
	int status;

	snprintf(src, sizeof src, "'%s' --exit-child 0 > no-such-directory-xyz/file", self);
	CHECK(run(src, &status) == 0);
	CHECK(status != 0);

	snprintf(src, sizeof src, "'%s' --exit-child 0 > no-such-directory-xyz/file | '%s' --exit-child 7", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 7); /* pipeline status is still the *last* command's */
}

/* 2.7.4: an unquoted heredoc delimiter gets a plain (no here-document
 * syntax involved) round trip through the command's stdin. */
static void test_exec_heredoc_basic(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512];
	int status;
	snprintf(src, sizeof src, "'%s' --stdin-eq hello <<EOF\nhello\nEOF\n", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
}

/* 2.7.4: "<<-" strips leading tabs from every body line and the
 * delimiter line -- already done by the parser (parse.c's
 * drain_heredocs()), so this is really testing that exec.c passes the
 * already-stripped r->heredoc straight through unmolested. */
static void test_exec_heredoc_dash_strips_tabs(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512];
	int status;
	snprintf(src, sizeof src, "'%s' --stdin-eq hi <<-EOF\n\t\thi\n\tEOF\n", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
}

/* 2.7.4: "If no part of word is quoted, all lines ... shall be
 * expanded for parameter expansion" -- reusing wordexp() (see
 * expand_heredoc() in src/sh/exec.c) rather than a second from-scratch
 * expander. */
static void test_exec_heredoc_unquoted_expands(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512];
	int status;
	setenv("SH_TEST_HEREDOC_VAR", "expanded", 1);
	snprintf(src, sizeof src, "'%s' --stdin-eq expanded <<EOF\n$SH_TEST_HEREDOC_VAR\nEOF\n", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
	unsetenv("SH_TEST_HEREDOC_VAR");
}

/* 2.7.4: "If any part of word is quoted ... the here-document lines
 * shall not be expanded" -- a quoted delimiter turns expansion off
 * entirely, even though the body text looks exactly like the unquoted
 * case above. */
static void test_exec_heredoc_quoted_delimiter_no_expansion(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512];
	int status;
	setenv("SH_TEST_HEREDOC_VAR", "expanded", 1);
	/* --stdin-eq's own operand is single-quoted so *it* is not
	 * expanded either -- both sides stay the literal three characters
	 * '$', 'V', ... i.e. the same unexpanded text. */
	snprintf(src, sizeof src, "'%s' --stdin-eq '$SH_TEST_HEREDOC_VAR' <<'EOF'\n$SH_TEST_HEREDOC_VAR\nEOF\n", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
	unsetenv("SH_TEST_HEREDOC_VAR");
}

/* 2.9.2: connects each command's stdout to the next one's stdin. */
static void test_exec_pipeline_two_stage(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512];
	int status;
	snprintf(src, sizeof src, "'%s' --produce hi | '%s' --stdin-eq hi", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
}

/* A pipeline of more than two commands: proves this is a real loop
 * over ncommands, not a hand-special-cased two-stage pipe. */
static void test_exec_pipeline_three_stage(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512];
	int status;
	snprintf(src, sizeof src, "'%s' --produce hi | '%s' --cat | '%s' --stdin-eq hi", self, self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
}

/* 2.9.2: "the exit status shall be the exit status of the last command
 * specified in the pipeline" -- even though an earlier stage "fails". */
static void test_exec_pipeline_status_is_last(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512];
	int status;
	snprintf(src, sizeof src, "'%s' --exit-child 3 | '%s' --exit-child 0", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);

	snprintf(src, sizeof src, "'%s' --exit-child 0 | '%s' --exit-child 9", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 9);
}

/* 2.9.2: "!" negates the exit status of the whole pipeline (i.e. of
 * its last command), for a pipeline of more than one command too --
 * test_pipeline_bang() (stage 1, above) only checks parsing. */
static void test_exec_pipeline_bang(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512];
	int status;
	snprintf(src, sizeof src, "! '%s' --exit-child 0 | '%s' --exit-child 5", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0); /* last command's status (5) was nonzero -> negated to 0 */

	snprintf(src, sizeof src, "! '%s' --exit-child 5 | '%s' --exit-child 0", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 1); /* last command's status (0) was zero -> negated to 1 */
}

/* A command's own redirections apply on top of the pipe hookup (2.7's
 * left-to-right ordering, same rule as test_exec_redir_dup_output_ordering
 * above): redirecting the *first* stage's stdout away from the pipe
 * means the next stage reads nothing from it. */
static void test_exec_pipeline_stage_redir_overrides_pipe(const char *self)
{
	if (!file_redir_supported(self)) return;
	char src[512], *tmp = make_tmp(), *got;
	int status;
	CHECK(tmp != 0);
	if (!tmp) return;

	snprintf(src, sizeof src, "'%s' --produce hi > %s | '%s' --stdin-eq-raw ''", self, tmp, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0); /* second stage got EOF with nothing written -- empty stdin */

	got = slurp(tmp);
	CHECK(got && strcmp(got, "hi\n") == 0); /* first stage's real output went to the file */
	free(got);
	remove(tmp);
	free(tmp);
}

/* ---- stage 4: subshells and brace groups (XCU 2.9.4, 2.12) -------------
 *
 * These build on run()/must_parse()/file_redir_supported()/make_tmp()/
 * slurp() from stages 2/3 above, plus self-exec roles already defined
 * there (--exit-child/--produce/--stdin-eq). The environment and
 * working-directory checks below inspect *this test process's own*
 * getenv()/getcwd() directly, rather than needing a child role, because
 * run() executes the parsed AST in this very process (exec_group() in
 * src/sh/exec.c does not fork for a standalone group -- see its header
 * comment for why); a group used as a pipeline stage does fork, and
 * test_exec_group_pipeline_stage() below checks specifically that even
 * then nothing leaks back into this process.
 */

/* Returns a malloc'd copy of the current working directory (never
 * NULL on success), for a test to restore afterward -- every test
 * below that changes the working directory (proving a subshell's `cd`
 * does not survive it, or a brace group's does) must put it back, or
 * later tests using cwd-relative temp files (make_tmp()) would break. */
static char *save_cwd(void)
{
	char *p = getcwd(0, 0);
	CHECK(p != 0);
	return p;
}

static void restore_cwd(char *saved)
{
	if (saved) { CHECK(chdir(saved) == 0); free(saved); }
}

/* 2.9.4: "{ compound-list ; }" executes "in the current process
 * environment" -- both a variable assignment and a `cd` inside it must
 * still be visible afterward. */
static void test_exec_brace_persists_assignment_and_cd(void)
{
	char *saved = save_cwd();
	char *now;
	int status;

	CHECK(mkdir("shtst_brace_dir", 0755) == 0 || errno == EEXIST);
	unsetenv("SH_TEST_BRACE_VAR");

	CHECK(run("{ SH_TEST_BRACE_VAR=hello; cd shtst_brace_dir; }", &status) == 0);
	CHECK(status == 0);

	{
		const char *v = getenv("SH_TEST_BRACE_VAR");
		CHECK(v && strcmp(v, "hello") == 0);
	}
	now = getcwd(0, 0);
	CHECK(now && saved && strcmp(now, saved) != 0); /* cd actually moved us */
	free(now);

	unsetenv("SH_TEST_BRACE_VAR");
	restore_cwd(saved);
	rmdir("shtst_brace_dir");
}

/* 2.9.4/2.12: "( compound-list )" executes in a subshell environment
 * that is a duplicate of the shell's; "changes made to the subshell
 * environment shall not affect the shell environment" -- the same
 * assignment and `cd` that persisted through a brace group above must
 * both vanish once the subshell finishes. */
static void test_exec_subshell_does_not_persist_assignment_and_cd(void)
{
	char *saved = save_cwd();
	char *now;
	int status;

	CHECK(mkdir("shtst_sub_dir", 0755) == 0 || errno == EEXIST);
	unsetenv("SH_TEST_SUB_VAR");

	CHECK(run("(SH_TEST_SUB_VAR=hello; cd shtst_sub_dir)", &status) == 0);
	CHECK(status == 0);

	CHECK(getenv("SH_TEST_SUB_VAR") == 0);
	now = getcwd(0, 0);
	CHECK(now && saved && strcmp(now, saved) == 0); /* still where we started */
	free(now);

	restore_cwd(saved);
	rmdir("shtst_sub_dir");
}

/* 2.9.4 Exit Status: "The exit status of a grouping command shall be
 * the exit status of compound-list" -- i.e. of the last command run in
 * it, for both forms. */
static void test_exec_group_exit_status(const char *self)
{
	char src[512];
	int status;

	snprintf(src, sizeof src, "{ '%s' --exit-child 3; '%s' --exit-child 9; }", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 9);

	snprintf(src, sizeof src, "( '%s' --exit-child 3; '%s' --exit-child 9 )", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 9);
}

/* A group's exit status feeds "!" negation and "&&"/"||" short-circuit
 * exactly like a pipeline's does (2.9.2/2.9.3 apply uniformly to
 * whatever kind of command produced the status) -- this is what "$?
 * propagates correctly out of each form" means operationally, since
 * this shell has no $? parameter expansion yet (sh.h's banner): the
 * *status* __sh_exec_*() threads through by reference is the only
 * observable stand-in for it, and these are the constructs that
 * consume it. */
static void test_exec_group_bang_and_status_propagation(const char *self)
{
	char src[512];
	int status;

	snprintf(src, sizeof src, "! ( '%s' --exit-child 0 )", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 1);

	snprintf(src, sizeof src, "! { '%s' --exit-child 5; }", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);

	snprintf(src, sizeof src, "( '%s' --exit-child 0 ) && '%s' --exit-child 9", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 9); /* left (the subshell) succeeded, right ran */

	snprintf(src, sizeof src, "( '%s' --exit-child 3 ) && '%s' --exit-child 9", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 3); /* left failed, right never ran -- status is still the left's */

	snprintf(src, sizeof src, "{ '%s' --exit-child 0; } || '%s' --exit-child 9", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0); /* left succeeded, right never ran */
}

/* 2.9.4: "each [may be followed by] redirections ... shall apply to all
 * the commands within the compound command" -- a single "> file" after
 * the group affects every command inside it, not just the last one
 * (which would instead be what redirecting *that one command* looks
 * like). */
static void test_exec_group_redir_whole(const char *self)
{
	char src[512], *tmp, *got;
	int status;

	if (!file_redir_supported(self)) return;
	tmp = make_tmp();
	CHECK(tmp != 0);
	if (!tmp) return;

	snprintf(src, sizeof src, "{ '%s' --produce one; '%s' --produce two; } > %s", self, self, tmp);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
	got = slurp(tmp);
	CHECK(got && strcmp(got, "one\ntwo\n") == 0);
	free(got);

	snprintf(src, sizeof src, "( '%s' --produce three; '%s' --produce four ) > %s", self, self, tmp);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
	got = slurp(tmp);
	CHECK(got && strcmp(got, "three\nfour\n") == 0);
	free(got);

	remove(tmp);
	free(tmp);
}

/* Nesting: a brace group's assignment is visible inside a subshell that
 * contains it (it is still "the current process environment" as far as
 * that subshell's own copy is concerned) but the outer subshell still
 * discards it on the way out; a subshell nested inside a brace group
 * never lets its assignment escape even that innermost boundary. */
static void test_exec_group_nesting(const char *self)
{
	char src[512];
	int status;

	unsetenv("SH_TEST_NEST_VAR");

	snprintf(src, sizeof src, "( { SH_TEST_NEST_VAR=inner; '%s' --exit-child 4; } )", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 4);
	CHECK(getenv("SH_TEST_NEST_VAR") == 0);

	snprintf(src, sizeof src, "{ (SH_TEST_NEST_VAR=inner2); '%s' --exit-child 6; }", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 6);
	CHECK(getenv("SH_TEST_NEST_VAR") == 0);
}

/* A group used as one stage of a multi-command pipeline: its stdout
 * must reach the next stage exactly like a simple command's would
 * (src/sh/exec.c's fork_group_stage()), and -- per 2.12's "each command
 * of a multi-command pipeline is in a subshell environment", which
 * applies regardless of "(...)" vs "{...}" -- an assignment inside a
 * *brace* group used this way must still not leak into the real shell,
 * even though a standalone brace group (tested above) does not isolate
 * at all. */
static void test_exec_group_pipeline_stage(const char *self)
{
	char src[512];
	int status;

	if (!file_redir_supported(self)) return;

	snprintf(src, sizeof src, "( '%s' --produce hi ) | '%s' --stdin-eq hi", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);

	snprintf(src, sizeof src, "{ '%s' --produce hi; } | '%s' --stdin-eq hi", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);

	/* pipeline status is still the *last* stage's, whether that stage
	 * is a group or a simple command (2.9.2). */
	snprintf(src, sizeof src, "'%s' --produce hi | ( '%s' --stdin-eq hi; '%s' --exit-child 7 )", self, self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 7);

	unsetenv("SH_TEST_PIPE_VAR");
	snprintf(src, sizeof src, "{ SH_TEST_PIPE_VAR=leaked; '%s' --exit-child 0; } | '%s' --exit-child 0", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
	CHECK(getenv("SH_TEST_PIPE_VAR") == 0);
	unsetenv("SH_TEST_PIPE_VAR");
}

/* Reads every remaining byte of stdin into a growable buffer.
 * *out_len receives the byte count (never counting a NUL this doesn't
 * add). Returns a malloc'd buffer (possibly zero-length, never NULL on
 * success) or NULL on a real read error. */
/* ---- stage 5: command substitution (XCU 2.6.3) --------------------------
 *
 * https://pubs.opengroup.org/onlinepubs/9699919799/utilities/V3_chap02.html
 *   2.6.3 Command Substitution   2.6.5 Field Splitting
 *   2.9.1 Simple Commands (the "no command name" exit-status rule)
 *   2.12 Shell Execution Environment (the subshell environment 2.6.3 cites)
 *
 * Every one of these needs the substituted command's standard output to
 * actually reach the capture __sh_cmdsub() sets up, which is a
 * redirection of *this* process's fd 1 that a spawned child has to
 * observe -- exactly what file_redir_supported() above tests for, so
 * everything that inspects captured text gates on it. The three
 * exit-status tests do not: a status comes back through waitpid()
 * whatever the child's fd 1 was.
 *
 * Both substitution forms are covered case for case, not one covered
 * and the other spot-checked. The backquoted form is not the awkward
 * sibling here: an autoconf-generated `configure` -- the concrete thing
 * a working sh on this platform would be handed -- uses backquotes
 * about fourteen times as often as "$(...)", so its own rules (2.6.3's
 * "first unquoted non-escaped backquote" search, its backslash removal,
 * and nesting *through* that backslash removal rather than through
 * matching parentheses) are the ones most likely to be exercised in
 * anger. Note also that every backtick test below has this binary's own
 * Windows path -- backslashes and all -- inside the backquotes, in
 * single quotes: 2.6.3's backslash rule removes a backslash only before
 * '$', '`' or another backslash, so "Z:\tmp\...\sh.exe" survives it
 * intact, and a rule that over-removed would break these tests loudly.
 */

/* Runs "'self' --produce-join WORDS > tmp" and returns what the child
 * received as argv[2..], comma-joined -- i.e. exactly the fields WORDS
 * expanded to, in order. Returns a malloc'd string the caller frees, or
 * NULL if the command could not be run at all; *status gets the
 * command's exit status. */
static char *fields_of(const char *self, const char *words, int *status)
{
	char src[1024], *tmp = make_tmp(), *got;
	if (!tmp) return 0;
	snprintf(src, sizeof src, "'%s' --produce-join %s > %s", self, words, tmp);
	if (run(src, status) != 0) { remove(tmp); free(tmp); return 0; }
	got = slurp(tmp);
	remove(tmp);
	free(tmp);
	return got;
}

/* 2.6.3: "The shell shall expand the command substitution by executing
 * command in a subshell environment ... and replacing the command
 * substitution (the text of command plus the enclosing "$()" or
 * backquotes) with the standard output of the command, removing
 * sequences of one or more <newline> characters at the end of the
 * substitution." */
static void test_exec_cmdsub_basic(const char *self)
{
	char words[512], *got;
	int status;

	if (!file_redir_supported(self)) return;

	snprintf(words, sizeof words, "$('%s' --produce hi)", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "hi") == 0);	/* not "hi\n" */
	CHECK(status == 0);
	free(got);

	snprintf(words, sizeof words, "`'%s' --produce hi`", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "hi") == 0);
	CHECK(status == 0);
	free(got);

	/* "sequences of one or more" -- three trailing newlines all go. */
	snprintf(words, sizeof words, "$('%s' --produce-trailing hi)", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "hi") == 0);
	free(got);

	snprintf(words, sizeof words, "`'%s' --produce-trailing hi`", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "hi") == 0);
	free(got);

	/* A substitution is part of the word it appears in, not a word of
	 * its own: text either side of it concatenates, and two of them in
	 * one word still make one word. */
	snprintf(words, sizeof words, "x$('%s' --produce hi)y", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "xhiy") == 0);
	free(got);

	snprintf(words, sizeof words, "x`'%s' --produce hi`y", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "xhiy") == 0);
	free(got);

	snprintf(words, sizeof words, "$('%s' --produce a)`'%s' --produce b`", self, self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "ab") == 0);
	free(got);
}

/* 2.6.3: "Embedded <newline> characters before the end of the output
 * shall not be removed; however, they may be treated as field
 * delimiters and eliminated during field splitting" (2.6.5), and "If a
 * command substitution occurs inside double-quotes, field splitting and
 * pathname expansion shall not be performed on the results of the
 * substitution."  So the *same* two-line output is two fields unquoted
 * and one field (newline intact) quoted. */
static void test_exec_cmdsub_field_splitting(const char *self)
{
	char words[512], *got;
	int status;

	if (!file_redir_supported(self)) return;

	snprintf(words, sizeof words, "$('%s' --produce-embedded a b)", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "a,b") == 0);	/* two fields */
	free(got);

	snprintf(words, sizeof words, "\"$('%s' --produce-embedded a b)\"", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "a\nb") == 0);	/* one field, newline kept */
	free(got);

	snprintf(words, sizeof words, "`'%s' --produce-embedded a b`", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "a,b") == 0);
	free(got);

	snprintf(words, sizeof words, "\"`'%s' --produce-embedded a b`\"", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "a\nb") == 0);
	free(got);

	/* Spaces in the output split the same way newlines do, and a
	 * substitution that produces nothing at all produces no field --
	 * "x $(nothing) y" is two words, not three. */
	snprintf(words, sizeof words, "$('%s' --produce 'p q r')", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "p,q,r") == 0);
	free(got);

	snprintf(words, sizeof words, "x $('%s' --produce-join '') y", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "x,y") == 0);
	free(got);
}

/* 2.6.3, same clause: pathname expansion is performed on an unquoted
 * substitution's result and not on a double-quoted one. Needs a real
 * file to match, created here rather than assumed. */
static void test_exec_cmdsub_pathname_expansion(const char *self)
{
	char words[512], *got;
	int status;
	FILE *f;

	if (!file_redir_supported(self)) return;

	f = fopen("shcsA1", "wb");
	CHECK(f != 0);
	if (!f) return;
	fclose(f);

	snprintf(words, sizeof words, "$('%s' --produce-join 'shcsA*')", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "shcsA1") == 0);
	free(got);

	snprintf(words, sizeof words, "\"$('%s' --produce-join 'shcsA*')\"", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "shcsA*") == 0);
	free(got);

	remove("shcsA1");
}

/* 2.6.3: "The results of command substitution shall not be processed
 * for further tilde expansion, parameter expansion, command
 * substitution, or arithmetic expansion." A '$NAME' in the output stays
 * six literal bytes even with NAME set. */
static void test_exec_cmdsub_result_not_reexpanded(const char *self)
{
	char words[512], *got;
	int status;

	if (!file_redir_supported(self)) return;
	setenv("SH_TEST_CMDSUB_VAR", "expanded", 1);

	snprintf(words, sizeof words, "$('%s' --produce-join '$SH_TEST_CMDSUB_VAR')", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "$SH_TEST_CMDSUB_VAR") == 0);
	free(got);

	snprintf(words, sizeof words, "`'%s' --produce-join '$SH_TEST_CMDSUB_VAR'`", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "$SH_TEST_CMDSUB_VAR") == 0);
	free(got);

	unsetenv("SH_TEST_CMDSUB_VAR");
}

/* 2.6.3: "Command substitution can be nested."  For "$(...)" that is
 * just matching parentheses; for the backquoted form, "To specify
 * nesting within the backquoted version, the application shall precede
 * the inner backquotes with <backslash> characters" -- which works only
 * because the same clause's backslash rule turns the escaped inner
 * backquotes back into real ones in the command text.
 *
 * Also 2.6.3's own worked example of the "$((" ambiguity: "a command
 * substitution containing a single subshell could be written as
 * $( (command) )" -- with the space, so it is not read as an arithmetic
 * expansion. */
static void test_exec_cmdsub_nesting(const char *self)
{
	char words[512], *got;
	int status;

	if (!file_redir_supported(self)) return;

	snprintf(words, sizeof words, "$('%s' --produce-join $('%s' --produce inner))", self, self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "inner") == 0);
	free(got);

	snprintf(words, sizeof words, "`'%s' --produce-join \\`'%s' --produce inner\\``", self, self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "inner") == 0);
	free(got);

	/* Three deep, backquoted: the inner-inner pair needs its
	 * backslashes escaped in turn ("\\\\`" in C is \\` in the shell
	 * source), which is exactly the doubling real scripts write. */
	snprintf(words, sizeof words,
	         "`'%s' --produce-join \\`'%s' --produce-join \\\\\\`'%s' --produce deep\\\\\\`\\``",
	         self, self, self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "deep") == 0);
	free(got);

	/* Mixed: a backquoted substitution inside a "$(...)" one, and the
	 * reverse. Both nest without any escaping at all, since neither
	 * form's terminator can be confused for the other's. */
	snprintf(words, sizeof words, "$('%s' --produce-join `'%s' --produce mixed1`)", self, self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "mixed1") == 0);
	free(got);

	snprintf(words, sizeof words, "`'%s' --produce-join $('%s' --produce mixed2)`", self, self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "mixed2") == 0);
	free(got);

	/* $( (command) ) -- 2.6.3's prescribed spelling for a substitution
	 * whose command is a subshell. */
	snprintf(words, sizeof words, "$( ('%s' --produce sub) )", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "sub") == 0);
	free(got);
}

/* 2.6.3: "Within the backquoted style of command substitution,
 * <backslash> shall retain its literal meaning, except when followed
 * by: '$', '`', or <backslash>."  Each of the three exceptions is
 * checked by making the *unescaped* and the *not-unescaped* readings
 * produce visibly different output -- a test that passed either way
 * would be worth nothing. "$(...)" is checked alongside each, where the
 * same bytes mean something else entirely because its command text is
 * taken verbatim. */
static void test_exec_cmdsub_backquote_backslash(const char *self)
{
	char words[512], *got;
	int status;

	if (!file_redir_supported(self)) return;
	setenv("SH_TEST_CMDSUB_VAR", "expanded", 1);

	/* \$ -> $ : the command text gets a live '$', so the *inner*
	 * command's word is a parameter expansion. Without the unescaping
	 * the inner word would be "\$SH_TEST_CMDSUB_VAR", whose backslash
	 * makes the '$' literal. */
	snprintf(words, sizeof words, "`'%s' --produce-join \\$SH_TEST_CMDSUB_VAR`", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "expanded") == 0);
	free(got);

	/* The same bytes inside "$(...)", where nothing is unescaped: the
	 * inner word really is "\$SH_TEST_CMDSUB_VAR" and the '$' stays
	 * literal. */
	snprintf(words, sizeof words, "$('%s' --produce-join \\$SH_TEST_CMDSUB_VAR)", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "$SH_TEST_CMDSUB_VAR") == 0);
	free(got);

	/* \\ -> \ : the command text gets one backslash, which the inner
	 * word's own quote removal then consumes, so "a\\b" here reaches
	 * the child as "ab". Not unescaping would leave two backslashes,
	 * of which quote removal eats one, giving "a\b". */
	snprintf(words, sizeof words, "`'%s' --produce-join a\\\\b`", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "ab") == 0);
	free(got);

	snprintf(words, sizeof words, "$('%s' --produce-join a\\\\b)", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "a\\b") == 0);
	free(got);

	/* A backslash before anything else keeps "its literal meaning" --
	 * it is passed through to the command text as a backslash, still
	 * escaping the character after it there. */
	snprintf(words, sizeof words, "`'%s' --produce-join a\\qb`", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "aqb") == 0);
	free(got);

	unsetenv("SH_TEST_CMDSUB_VAR");
}

/* 2.2.3 Double-Quotes: "The backquote shall retain its special meaning
 * introducing the other form of command substitution", and inside
 * double-quotes <backslash> is special before '$', '`', '"', '\' and
 * <newline> -- so an escaped backquote there is a literal backquote,
 * not the start of a substitution. */
static void test_exec_cmdsub_in_double_quotes(const char *self)
{
	char words[512], *got;
	int status;

	if (!file_redir_supported(self)) return;

	snprintf(words, sizeof words, "\"pre `'%s' --produce mid` post\"", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "pre mid post") == 0);	/* one field */
	free(got);

	snprintf(words, sizeof words, "\"pre $('%s' --produce mid) post\"", self);
	got = fields_of(self, words, &status);
	CHECK(got && strcmp(got, "pre mid post") == 0);
	free(got);

	/* An escaped backquote inside double-quotes is data. */
	got = fields_of(self, "\"a\\`b\"", &status);
	CHECK(got && strcmp(got, "a`b") == 0);
	free(got);

	/* Single quotes make both forms inert (2.2.2: "each character
	 * within the single-quotes retains its literal value"). */
	got = fields_of(self, "'$(echo hi)' '`echo hi`'", &status);
	CHECK(got && strcmp(got, "$(echo hi),`echo hi`") == 0);
	free(got);
}

/* 2.9.1: "If there is a command name, execution shall continue as
 * described in Command Search and Execution. If there is no command
 * name, but the command contained a command substitution, the command
 * shall complete with the exit status of the last command substitution
 * performed. Otherwise, the command shall complete with a zero exit
 * status."
 *
 * No file_redir_supported() gate: a status comes back through
 * waitpid() regardless of where the child's output went. */
static void test_exec_cmdsub_exit_status(const char *self)
{
	char src[512];
	int status;

	/* No command name at all -- the whole command is one substitution
	 * that produces no output, so no field survives expansion. */
	snprintf(src, sizeof src, "$('%s' --exit-child 7)", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 7);

	snprintf(src, sizeof src, "`'%s' --exit-child 6`", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 6);

	/* "the *last* command substitution performed", not the first. */
	snprintf(src, sizeof src, "$('%s' --exit-child 3)$('%s' --exit-child 5)", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 5);

	/* A substitution in an assignment's value is one the command
	 * "contained" (2.9.1 step 4 expands assignment values for command
	 * substitution), and the assignment still takes effect. */
	snprintf(src, sizeof src, "SH_TEST_CMDSUB_ST=$('%s' --exit-child 4)", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 4);
	unsetenv("SH_TEST_CMDSUB_ST");

	/* With a command name, the *command's* status wins outright. */
	snprintf(src, sizeof src, "'%s' --exit-child 2 $('%s' --exit-child 9)", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 2);

	/* A substitution whose command cannot be found is not an error:
	 * it produces no output and exits 127, exactly like any other
	 * command not found (matching test_exec_command_not_found()). */
	CHECK(run("$(this-program-genuinely-does-not-exist-xyz)", &status) == 0);
	CHECK(status == 127);
}

/* 2.6.3: "executing command in a subshell environment (see Shell
 * Execution Environment)", i.e. 2.12's -- the same one "( list )" gets,
 * which src/sh/exec.c implements by reusing exec_group()'s own
 * save-and-restore rather than a second mechanism. So a substituted
 * command's assignment and its `cd` must both be gone afterwards, the
 * way test_exec_subshell_does_not_persist_assignment_and_cd() already
 * checks for the parenthesised form. */
static void test_exec_cmdsub_subshell_environment(const char *self)
{
	char src[512], *cwd_before, *cwd_after;
	int status;

	unsetenv("SH_TEST_CMDSUB_LEAK");
	CHECK(run("$(SH_TEST_CMDSUB_LEAK=leaked)", &status) == 0);
	CHECK(getenv("SH_TEST_CMDSUB_LEAK") == 0);

	CHECK(run("`SH_TEST_CMDSUB_LEAK=leaked`", &status) == 0);
	CHECK(getenv("SH_TEST_CMDSUB_LEAK") == 0);

	cwd_before = getcwd(0, 0);
	CHECK(cwd_before != 0);
	CHECK(run("$(cd ..)", &status) == 0);
	cwd_after = getcwd(0, 0);
	CHECK(cwd_before && cwd_after && strcmp(cwd_before, cwd_after) == 0);
	free(cwd_before);
	free(cwd_after);
	(void)self;
}

/* 2.7: "the word that follows the redirection operator shall be
 * subjected to tilde expansion, parameter expansion, command
 * substitution, arithmetic expansion, and quote removal" -- and 2.7.4,
 * for an unquoted here-document delimiter, "all lines of the
 * here-document shall be expanded for parameter expansion, command
 * substitution, and arithmetic expansion". Both of these were the
 * -1 "not implemented at this stage" cases stage 4's
 * test_exec_reports_unimplemented_constructs() asserted. */
static void test_exec_cmdsub_in_redirections(const char *self)
{
	char src[1024], *tmp, *got;
	int status;

	if (!file_redir_supported(self)) return;

	tmp = make_tmp();
	CHECK(tmp != 0);
	if (!tmp) return;

	/* The redirection target itself comes from a substitution. */
	snprintf(src, sizeof src, "'%s' --produce hi > $('%s' --produce-join %s)", self, self, tmp);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
	got = slurp(tmp);
	CHECK(got && strcmp(got, "hi\n") == 0);
	free(got);

	snprintf(src, sizeof src, "'%s' --produce ho > `'%s' --produce-join %s`", self, self, tmp);
	CHECK(run(src, &status) == 0);
	got = slurp(tmp);
	CHECK(got && strcmp(got, "ho\n") == 0);
	free(got);

	/* ... including when the redirection belongs to a compound command
	 * rather than a simple one. */
	snprintf(src, sizeof src, "('%s' --produce grp) > $('%s' --produce-join %s)", self, self, tmp);
	CHECK(run(src, &status) == 0);
	got = slurp(tmp);
	CHECK(got && strcmp(got, "grp\n") == 0);
	free(got);

	snprintf(src, sizeof src, "{ '%s' --produce brc > $('%s' --produce-join %s); }", self, self, tmp);
	CHECK(run(src, &status) == 0);
	got = slurp(tmp);
	CHECK(got && strcmp(got, "brc\n") == 0);
	free(got);

	remove(tmp);
	free(tmp);

	/* An unquoted here-document body expands substitutions (2.7.4) ... */
	snprintf(src, sizeof src, "'%s' --stdin-eq hi <<EOF\n$('%s' --produce hi)\nEOF\n", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);

	snprintf(src, sizeof src, "'%s' --stdin-eq hi <<EOF\n`'%s' --produce hi`\nEOF\n", self, self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);

	/* ... and a quoted delimiter suppresses them entirely, the body
	 * arriving as the literal source bytes. */
	snprintf(src, sizeof src, "'%s' --stdin-eq '$(x)' <<'EOF'\n$(x)\nEOF\n", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
}

/* The -1 "cannot execute this AST node at all" convention (sh.h)
 * survives stage 5: it now means the *substituted* list used a
 * construct this executor still refuses, not that substitution itself
 * is unimplemented. src/sh/exec.c refuses two directly adjacent
 * compound-command pipeline stages (it would deadlock without a
 * fork()), which makes a usable, stable example of one. */
static void test_exec_cmdsub_propagates_unimplemented(const char *self)
{
	char src[512];
	int status;

	snprintf(src, sizeof src, "'%s' --produce-join $( ('%s' --produce a) | { '%s' --cat; } )",
	         self, self, self);
	CHECK(run(src, &status) == -1);

	/* A syntax error inside the substitution is reported the same way
	 * (wordexp() turns it into WRDE_SYNTAX; exec.c cannot tell the two
	 * apart and does not need to). */
	CHECK(run("$( | )", &status) == -1);
	CHECK(run("` | `", &status) == -1);
}

static char *read_all_stdin(size_t *out_len)
{
	size_t cap = 256, len = 0;
	char *buf = malloc(cap);
	if (!buf) return 0;
	for (;;) {
		size_t n;
		if (len == cap) {
			char *nb = realloc(buf, cap *= 2);
			if (!nb) { free(buf); return 0; }
			buf = nb;
		}
		n = fread(buf + len, 1, cap - len, stdin);
		len += n;
		if (n == 0) break;
	}
	*out_len = len;
	return buf;
}


/* ---- stage 6a: the built-in dispatcher and its utilities --------------
 *
 * Spec pages: XCU 2.9.1 "Command Search and Execution", XCU 2.14
 * "Special Built-In Utilities", XCU test(1p) (OPERANDS + EXTENDED
 * DESCRIPTION + EXIT STATUS), XCU true(1p)/false(1p), XCU cd(1p).
 */

/* 2.9.1 step 1: the command name a built-in is matched against is the
 * one that exists *after* the word expansions, not the raw source text
 * -- which is precisely what src/sh/exec.c's old raw strcmp("cd") could
 * not express.  Both a quoted 'cd' and a $VAR expanding to "cd" have to
 * reach the built-in. */
static void test_builtin_dispatch_uses_expanded_name(void)
{
	char *saved = save_cwd();
	char *now;
	int status;

	CHECK(mkdir("shtst_disp_dir", 0755) == 0 || errno == EEXIST);

	CHECK(run("'cd' shtst_disp_dir", &status) == 0);
	CHECK(status == 0);
	now = getcwd(0, 0);
	CHECK(now && saved && strcmp(now, saved) != 0);
	free(now);
	CHECK(chdir(saved) == 0);

	CHECK(run("SHT_CDNAME=cd; $SHT_CDNAME shtst_disp_dir", &status) == 0);
	CHECK(status == 0);
	now = getcwd(0, 0);
	CHECK(now && saved && strcmp(now, saved) != 0);
	free(now);

	unsetenv("SHT_CDNAME");
	restore_cwd(saved);
	rmdir("shtst_disp_dir");
}

/* 2.14 ":" -- "This utility shall only expand command arguments... EXIT
 * STATUS: Zero."  true(1p): "shall return with exit code zero";
 * false(1p): "shall return with a non-zero exit code". */
static void test_builtin_colon_true_false(void)
{
	int status;

	CHECK(run(":", &status) == 0);
	CHECK(status == 0);
	CHECK(run(": ignored arguments here", &status) == 0);
	CHECK(status == 0);

	CHECK(run("true", &status) == 0);
	CHECK(status == 0);
	CHECK(run("false", &status) == 0);
	CHECK(status != 0);

	/* Regular utilities on a POSIX system, but there is no true.exe on
	 * this platform, so before stage 6a every one of these was a
	 * "command not found" 127 -- the value that must never come back. */
	CHECK(run("true", &status) == 0 && status != 127);
	CHECK(run("test x = x", &status) == 0 && status != 127);
}

/* 2.14 "exit [n]": "shall cause the shell to exit with the exit status
 * specified by the unsigned decimal integer n... If n is not specified,
 * the exit status shall be that of the last command executed."  The
 * "cause the shell to exit" half is observable here as the rest of the
 * list not running. */
static void test_builtin_exit(const char *self)
{
	char src[512];
	int status;

	CHECK(run("exit 7", &status) == 0);
	CHECK(status == 7);

	/* Everything after it must not run: the marker file would exist if
	 * the second list item had executed. */
	unlink("shtst_exit_marker.txt");
	snprintf(src, sizeof src, "exit 4; '%s' --produce x > shtst_exit_marker.txt", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 4);
	CHECK(access("shtst_exit_marker.txt", F_OK) != 0);

	/* ... including across "&&"/"||", where the status would otherwise
	 * have selected the next pipeline. */
	snprintf(src, sizeof src, "exit 0 && '%s' --produce x > shtst_exit_marker.txt", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);
	CHECK(access("shtst_exit_marker.txt", F_OK) != 0);

	/* "If n is not specified, the exit status shall be that of the
	 * last command executed" (2.14, and 2.8.2 for what that means). */
	snprintf(src, sizeof src, "'%s' --exit-child 6; exit", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 6);
	snprintf(src, sizeof src, "'%s' --exit-child 0; exit", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);

	/* 2.9.4/2.12: "( ... )" runs in a subshell environment, so an
	 * `exit` inside it exits that subshell only -- the list continues,
	 * and its status is the *later* command's. */
	snprintf(src, sizeof src, "( exit 3 ); '%s' --exit-child 5", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 5);

	/* And a command substitution is a subshell environment too
	 * (2.6.3), so "$(exit 3)" must not take the shell down with it. */
	snprintf(src, sizeof src, "SHT_X=$(exit 3); '%s' --exit-child 5", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 5);
	unsetenv("SHT_X");

	/* A brace group is *not* a subshell (2.9.4: "in the current
	 * process environment"), so this one really does end the program. */
	unlink("shtst_exit_marker.txt");
	snprintf(src, sizeof src, "{ exit 2; }; '%s' --produce x > shtst_exit_marker.txt", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 2);
	CHECK(access("shtst_exit_marker.txt", F_OK) != 0);

	/* The unwind must not latch: a second program in the same process
	 * has to run normally afterwards. */
	CHECK(run("true", &status) == 0);
	CHECK(status == 0);
}

/* test(1p) EXTENDED DESCRIPTION: "The algorithm for determining the
 * precedence of the operators and the return value that shall be
 * generated is based on the number of arguments presented to test."
 * These are the 0-, 1-, 2-, 3- and 4-argument rules taken one at a
 * time; each is a case a "tokenise and evaluate" implementation gets
 * wrong. */
static void test_builtin_test_argc_rules(void)
{
	int status;

	/* "0 arguments: Exit false (1)." */
	CHECK(run("test", &status) == 0);
	CHECK(status == 1);

	/* "1 argument: Exit true (0) if $1 is not null; otherwise, exit
	 * false." -- note "-f" alone is a *string*, hence true. */
	CHECK(run("test x", &status) == 0);
	CHECK(status == 0);
	CHECK(run("test ''", &status) == 0);
	CHECK(status == 1);
	CHECK(run("test -f", &status) == 0);
	CHECK(status == 0);

	/* "2 arguments: If $1 is '!', exit true if $2 is null, false if $2
	 * is not null." -- a string test of $2, not a negated evaluation
	 * of it, so "! -f" is false because "-f" is a non-null string. */
	CHECK(run("test ! ''", &status) == 0);
	CHECK(status == 0);
	CHECK(run("test ! x", &status) == 0);
	CHECK(status == 1);
	CHECK(run("test ! -f", &status) == 0);
	CHECK(status == 1);
	/* "If $1 is a unary primary, exit true if the unary test is
	 * true" */
	CHECK(run("test -d .", &status) == 0);
	CHECK(status == 0);

	/* "3 arguments: If $2 is a binary primary, perform the binary test
	 * of $1 and $3." -- checked *before* the '('/')' rule, which is
	 * what makes this a string comparison rather than a group. */
	CHECK(run("test '(' = ')'", &status) == 0);
	CHECK(status == 1);
	CHECK(run("test '(' = '('", &status) == 0);
	CHECK(status == 0);
	/* "If $1 is '!', negate the two-argument test of $2 and $3." */
	CHECK(run("test ! -d .", &status) == 0);
	CHECK(status == 1);
	CHECK(run("test ! -d shtst_no_such_dir", &status) == 0);
	CHECK(status == 0);
	/* XSI: "If $1 is '(' and $3 is ')', perform the unary test of
	 * $2" -- i.e. the one-argument non-null-string test. */
	CHECK(run("test '(' x ')'", &status) == 0);
	CHECK(status == 0);
	CHECK(run("test '(' '' ')'", &status) == 0);
	CHECK(status == 1);

	/* "4 arguments: If $1 is '!', negate the three-argument test of
	 * $2, $3, and $4." */
	CHECK(run("test ! a = a", &status) == 0);
	CHECK(status == 1);
	CHECK(run("test ! a = b", &status) == 0);
	CHECK(status == 0);
	/* XSI: "If $1 is '(' and $4 is ')', perform the two-argument test
	 * of $2 and $3." */
	CHECK(run("test '(' -d . ')'", &status) == 0);
	CHECK(status == 0);
}

/* test(1p) OPERANDS, the file-system primaries. */
static void test_builtin_test_file_primaries(void)
{
	int status;
	FILE *f;

	unlink("shtst_t_file.txt");
	unlink("shtst_t_empty.txt");
	rmdir("shtst_t_dir");
	CHECK(mkdir("shtst_t_dir", 0755) == 0 || errno == EEXIST);
	f = fopen("shtst_t_file.txt", "wb");
	CHECK(f != 0);
	if (f) { fputs("data", f); fclose(f); }
	f = fopen("shtst_t_empty.txt", "wb");
	CHECK(f != 0);
	if (f) fclose(f);

	/* "-e pathname: True if pathname resolves to an existing directory
	 * entry.  False if pathname cannot be resolved." */
	CHECK(run("test -e shtst_t_file.txt", &status) == 0 && status == 0);
	CHECK(run("test -e shtst_t_dir", &status) == 0 && status == 0);
	CHECK(run("test -e shtst_t_nothing", &status) == 0 && status == 1);

	/* "-f ... for a regular file" / "-d ... for a directory" -- the
	 * two must disagree about the same two operands, which a
	 * -e-for-everything implementation would fail. */
	CHECK(run("test -f shtst_t_file.txt", &status) == 0 && status == 0);
	CHECK(run("test -f shtst_t_dir", &status) == 0 && status == 1);
	CHECK(run("test -d shtst_t_dir", &status) == 0 && status == 0);
	CHECK(run("test -d shtst_t_file.txt", &status) == 0 && status == 1);
	CHECK(run("test -f shtst_t_nothing", &status) == 0 && status == 1);
	CHECK(run("test -d shtst_t_nothing", &status) == 0 && status == 1);

	/* "-s ... a file that has a size greater than zero" */
	CHECK(run("test -s shtst_t_file.txt", &status) == 0 && status == 0);
	CHECK(run("test -s shtst_t_empty.txt", &status) == 0 && status == 1);
	CHECK(run("test -s shtst_t_nothing", &status) == 0 && status == 1);

	/* "-r ... permission to read from the file will be granted" /
	 * "-w ... permission to write" -- and both false for a pathname
	 * that "cannot be resolved". */
	CHECK(run("test -r shtst_t_file.txt", &status) == 0 && status == 0);
	CHECK(run("test -w shtst_t_file.txt", &status) == 0 && status == 0);
	CHECK(run("test -r shtst_t_nothing", &status) == 0 && status == 1);
	CHECK(run("test -w shtst_t_nothing", &status) == 0 && status == 1);

	/* "-x ... permission to execute the file (or search it, if it is a
	 * directory) will be granted" -- a directory this process just
	 * created and can chdir() into is searchable. */
	CHECK(run("test -x shtst_t_dir", &status) == 0 && status == 0);
	CHECK(run("test -x shtst_t_nothing", &status) == 0 && status == 1);

	/* "-z string: True if the length of string string is zero" /
	 * "-n string: True if the length of string is non-zero". */
	CHECK(run("test -z ''", &status) == 0 && status == 0);
	CHECK(run("test -z x", &status) == 0 && status == 1);
	CHECK(run("test -n ''", &status) == 0 && status == 1);
	CHECK(run("test -n x", &status) == 0 && status == 0);

	unlink("shtst_t_file.txt");
	unlink("shtst_t_empty.txt");
	rmdir("shtst_t_dir");
}

/* test(1p) OPERANDS: the string and arithmetic binary primaries, and
 * EXIT STATUS: ">1  An error occurred" for an operand of an arithmetic
 * primary that is not an integer.  A malformed expression must not
 * quietly become "false" -- 1 and 2 are different answers. */
static void test_builtin_test_binary_primaries(void)
{
	int status;

	CHECK(run("test abc = abc", &status) == 0 && status == 0);
	CHECK(run("test abc = abd", &status) == 0 && status == 1);
	CHECK(run("test abc != abd", &status) == 0 && status == 0);
	CHECK(run("test abc != abc", &status) == 0 && status == 1);

	CHECK(run("test 1 -eq 1", &status) == 0 && status == 0);
	CHECK(run("test 1 -eq 2", &status) == 0 && status == 1);
	CHECK(run("test 1 -ne 2", &status) == 0 && status == 0);
	CHECK(run("test 1 -lt 2", &status) == 0 && status == 0);
	CHECK(run("test 2 -lt 1", &status) == 0 && status == 1);
	CHECK(run("test 2 -le 2", &status) == 0 && status == 0);
	CHECK(run("test 3 -gt 2", &status) == 0 && status == 0);
	CHECK(run("test 2 -gt 3", &status) == 0 && status == 1);
	CHECK(run("test 3 -ge 3", &status) == 0 && status == 0);
	CHECK(run("test 2 -ge 3", &status) == 0 && status == 1);
	/* "algebraically" -- a negative integer is a valid operand, and a
	 * string comparison of "-1" and "1" would get this backwards. */
	CHECK(run("test -1 -lt 1", &status) == 0 && status == 0);

	/* An arithmetic primary whose operand is not an integer is an
	 * error (>1), not false.  The diagnostic goes to stderr, which is
	 * redirected here so a passing run stays quiet -- and doing that
	 * through the shell's own "2>" is one more thing being tested. */
	CHECK(run("test 1 -eq x 2>shtst_t_err.txt", &status) == 0);
	CHECK(status > 1);
	CHECK(run("test x -eq 1 2>shtst_t_err.txt", &status) == 0);
	CHECK(status > 1);
	/* A missing operator is equally an error, not false. */
	CHECK(run("test -d 2>shtst_t_err.txt", &status) == 0 && status == 0); /* 1 arg: non-null string */
	CHECK(run("test a b 2>shtst_t_err.txt", &status) == 0 && status > 1);
	unlink("shtst_t_err.txt");
}

/* test(1p) OPERANDS: "!", "-a", "-o" and "( expression )", plus the XSI
 * paragraph under ">4 arguments" that fixes their precedence -- "-a ...
 * has a higher precedence than -o", both left associative, and "the
 * string comparison binary primaries '=' and '!=' shall have a higher
 * precedence than any unary primary". */
static void test_builtin_test_grammar(void)
{
	int status;

	CHECK(run("test -n a -a -n b", &status) == 0 && status == 0);
	CHECK(run("test -n a -a -z b", &status) == 0 && status == 1);
	CHECK(run("test -z a -o -n b", &status) == 0 && status == 0);
	CHECK(run("test -z a -o -z b", &status) == 0 && status == 1);

	/* "-a ... has a higher precedence than -o": "false -a false -o
	 * true" is "(false -a false) -o true" = true.  Read the other way
	 * round it would be "false -a (false -o true)" = false. */
	CHECK(run("test -z a -a -z b -o -n c", &status) == 0 && status == 0);
	/* and "true -o true -a false" is "true -o (true -a false)" = true;
	 * left-to-right without precedence would give false. */
	CHECK(run("test -n a -o -n b -a -z c", &status) == 0 && status == 0);

	/* "( expression ): ... The parentheses can be used to alter the
	 * normal precedence and associativity." */
	CHECK(run("test '(' -z a -o -n b ')' -a -n c", &status) == 0 && status == 0);
	CHECK(run("test '(' -n a -o -n b ')' -a -z c", &status) == 0 && status == 1);

	/* "! expression: True if expression is false." */
	CHECK(run("test ! -n a -a -n b", &status) == 0 && status == 1);
	CHECK(run("test ! -z a -a -n b", &status) == 0 && status == 0);

	/* The XSI '='-binds-tighter-than-a-unary-primary rule: with the
	 * checks the other way round, "-n" would be taken as a unary
	 * primary applied to "=" and the "-n" on the right would never be
	 * seen as its operand. */
	CHECK(run("test -n = -n -o -z x", &status) == 0 && status == 0);
	CHECK(run("test -n = -z -o -z x", &status) == 0 && status == 1);

	/* An unbalanced group is an error (>1), not false. */
	CHECK(run("test '(' -n a -a -n b 2>shtst_t_err.txt", &status) == 0 && status > 1);
	unlink("shtst_t_err.txt");
}

/* test(1p) DESCRIPTION: "In the second form of the utility, where the
 * utility name used is [ rather than test, the application shall ensure
 * that the closing square bracket is a separate argument", and
 * EXTENDED DESCRIPTION: "when using the '[...]' form, the
 * <right-square-bracket> final argument shall not be counted in this
 * algorithm". */
static void test_builtin_bracket(void)
{
	int status;

	CHECK(run("[ -d . ]", &status) == 0 && status == 0);
	CHECK(run("[ -d shtst_no_such_dir ]", &status) == 0 && status == 1);
	CHECK(run("[ ]", &status) == 0 && status == 1);         /* 0 arguments after ']' is dropped */
	CHECK(run("[ x ]", &status) == 0 && status == 0);
	CHECK(run("[ '(' = ')' ]", &status) == 0 && status == 1); /* still the 3-argument rule */

	/* Missing ']' is an error, not a silent evaluation of the rest. */
	CHECK(run("[ -d . 2>shtst_t_err.txt", &status) == 0 && status > 1);
	unlink("shtst_t_err.txt");
}

/* 2.9.1: a built-in must be reachable from every place a command is,
 * not just as a whole program -- as a pipeline stage, inside a group,
 * and as an and-or term.  2.12 additionally puts each stage of a
 * multi-command pipeline in a subshell environment, so a `cd` there
 * must not move the *shell*. */
static void test_builtin_in_compound_contexts(const char *self)
{
	char src[512];
	char *saved = save_cwd();
	char *now;
	int status;

	CHECK(run("{ true; }", &status) == 0 && status == 0);
	CHECK(run("( false )", &status) == 0 && status == 1);
	CHECK(run("true && test 1 -eq 1", &status) == 0 && status == 0);
	CHECK(run("false || test 1 -eq 1", &status) == 0 && status == 0);
	CHECK(run("! true", &status) == 0 && status == 1);

	snprintf(src, sizeof src, "'%s' --produce hi | test -n x", self);
	CHECK(run(src, &status) == 0);
	CHECK(status == 0);

	CHECK(mkdir("shtst_pipe_dir", 0755) == 0 || errno == EEXIST);
	snprintf(src, sizeof src, "cd shtst_pipe_dir | '%s' --exit-child 0", self);
	CHECK(run(src, &status) == 0);
	now = getcwd(0, 0);
	CHECK(now && saved && strcmp(now, saved) == 0); /* the shell did not move */
	free(now);
	restore_cwd(saved);
	rmdir("shtst_pipe_dir");
}

/* Every self-exec role stage 2/3's tests use, dispatched on argv[1].
 * Returns the process's exit code for a recognised role, or -1 if
 * argv[1] is not one of them (so main() falls through to running the
 * actual test suite). */
static int child_role(int argc, char **argv)
{
	const char *role = argv[1];

	if (!strcmp(role, "--exit-child") && argc > 2)
		return atoi(argv[2]);

	/* Writes TEXT (argv[2]) plus a newline to stdout. Stands in for
	 * `echo` (not present as a standalone program on this platform --
	 * see this file's header comment). */
	if (!strcmp(role, "--produce") && argc > 2) {
		printf("%s\n", argv[2]);
		return 0;
	}

	/* Writes OUT (argv[2]) + "\n" to stdout and ERR (argv[3]) + "\n"
	 * to stderr, in that order -- for tests that need to distinguish
	 * which descriptor a byte went through (2>&1 ordering). */
	if (!strcmp(role, "--produce-both") && argc > 3) {
		fprintf(stdout, "%s\n", argv[2]);
		fflush(stdout);
		fprintf(stderr, "%s\n", argv[3]);
		fflush(stderr);
		return 0;
	}

	/* Writes its remaining arguments (argv[2..]) to stdout joined by
	 * <comma>, with NO trailing newline. Stage 5's workhorse: what a
	 * command substitution expanded to is a question about *fields*
	 * (how many, and which bytes in each), and an argv joined by a
	 * byte that neither IFS nor any expansion can produce answers it
	 * exactly, where "--produce hi" could only ever show one word's
	 * worth. The missing trailing newline is what makes the captured
	 * text an exact byte-for-byte assertion rather than one that
	 * silently tolerates 2.6.3's newline stripping being wrong. */
	if (!strcmp(role, "--produce-join") && argc > 2) {
		int i;
		for (i = 2; i < argc; i++) {
			if (i > 2) fputc(',', stdout);
			fputs(argv[i], stdout);
		}
		return 0;
	}

	/* Writes TEXT (argv[2]) followed by three newlines -- 2.6.3 strips
	 * "sequences of one or more <newline> characters at the end of the
	 * substitution", and one newline cannot tell a correct
	 * implementation from one that strips exactly one. */
	if (!strcmp(role, "--produce-trailing") && argc > 2) {
		printf("%s\n\n\n", argv[2]);
		return 0;
	}

	/* Writes A (argv[2]), a newline, B (argv[3]), a newline: an
	 * *embedded* newline, which 2.6.3 says is not removed but may act
	 * as a field delimiter, plus a trailing one, which is removed. */
	if (!strcmp(role, "--produce-embedded") && argc > 3) {
		printf("%s\n%s\n", argv[2], argv[3]);
		return 0;
	}

	/* Byte-for-byte stdin -> stdout, unbuffered assumptions aside.
	 * Stands in for `cat`. */
	if (!strcmp(role, "--cat")) {
		char buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, stdin)) > 0)
			fwrite(buf, 1, n, stdout);
		return 0;
	}

	/* Exits 0 iff stdin is exactly TEXT (argv[2]) followed by a single
	 * newline -- the shape every --produce output and every
	 * single-line here-document body (parse.c always appends '\n' per
	 * line) takes. */
	if (!strcmp(role, "--stdin-eq") && argc > 2) {
		size_t len, wantlen = strlen(argv[2]);
		char *got = read_all_stdin(&len);
		int ok;
		if (!got) return 5;
		ok = len == wantlen + 1 && memcmp(got, argv[2], wantlen) == 0 && got[wantlen] == '\n';
		if (!ok) fprintf(stderr, "child: stdin %.*s (%lu bytes), wanted %s\\n\n",
		                  (int)len, got, (unsigned long)len, argv[2]);
		free(got);
		return ok ? 0 : 4;
	}

	/* Like --stdin-eq but an exact byte-for-byte match, no implied
	 * trailing newline -- for asserting stdin is truly empty (0
	 * bytes), which "TEXT + '\\n'" can never express. */
	if (!strcmp(role, "--stdin-eq-raw") && argc > 2) {
		size_t len, wantlen = strlen(argv[2]);
		char *got = read_all_stdin(&len);
		int ok;
		if (!got) return 5;
		ok = len == wantlen && memcmp(got, argv[2], wantlen) == 0;
		free(got);
		return ok ? 0 : 4;
	}

	/* Exits 1 if fd N (argv[2]) is a currently-open descriptor, 0 if
	 * it is closed (EBADF) -- for "<&-"/">&-" tests, where the two
	 * exit codes read backwards from what a shell test usually wants
	 * is deliberate: 0 is easier to CHECK() against a specific
	 * "closed" expectation without a double negative in the caller. */
	if (!strcmp(role, "--fd-open") && argc > 2) {
		int fd = atoi(argv[2]);
		return fcntl(fd, F_GETFD) < 0 ? 0 : 1;
	}

	return -1;
}

int main(int argc, char **argv)
{
	if (argc > 1) { int r = child_role(argc, argv); if (r >= 0) return r; }

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
	test_exec_reports_unimplemented_constructs(argv[0]);

	test_exec_redir_output_creates_and_truncates(argv[0]);
	test_exec_redir_append(argv[0]);
	test_exec_redir_clobber_like_great(argv[0]);
	test_exec_redir_input(argv[0]);
	test_exec_redir_lessgreat_no_truncate(argv[0]);
	test_exec_redir_order_last_wins(argv[0]);
	test_exec_redir_dup_output_ordering(argv[0]);
	test_exec_redir_close(argv[0]);
	test_exec_redir_dup_closed_fd_fails(argv[0]);
	test_exec_redir_open_failure_nonabort(argv[0]);

	test_exec_heredoc_basic(argv[0]);
	test_exec_heredoc_dash_strips_tabs(argv[0]);
	test_exec_heredoc_unquoted_expands(argv[0]);
	test_exec_heredoc_quoted_delimiter_no_expansion(argv[0]);

	test_exec_pipeline_two_stage(argv[0]);
	test_exec_pipeline_three_stage(argv[0]);
	test_exec_pipeline_status_is_last(argv[0]);
	test_exec_pipeline_bang(argv[0]);
	test_exec_pipeline_stage_redir_overrides_pipe(argv[0]);

	test_exec_brace_persists_assignment_and_cd();
	test_exec_subshell_does_not_persist_assignment_and_cd();
	test_exec_group_exit_status(argv[0]);
	test_exec_group_bang_and_status_propagation(argv[0]);
	test_exec_group_redir_whole(argv[0]);
	test_exec_group_nesting(argv[0]);
	test_exec_group_pipeline_stage(argv[0]);

	test_exec_cmdsub_basic(argv[0]);
	test_exec_cmdsub_field_splitting(argv[0]);
	test_exec_cmdsub_pathname_expansion(argv[0]);
	test_exec_cmdsub_result_not_reexpanded(argv[0]);
	test_exec_cmdsub_nesting(argv[0]);
	test_exec_cmdsub_backquote_backslash(argv[0]);
	test_exec_cmdsub_in_double_quotes(argv[0]);
	test_exec_cmdsub_exit_status(argv[0]);
	test_exec_cmdsub_subshell_environment(argv[0]);
	test_exec_cmdsub_in_redirections(argv[0]);
	test_exec_cmdsub_propagates_unimplemented(argv[0]);

	test_builtin_dispatch_uses_expanded_name();
	test_builtin_colon_true_false();
	test_builtin_exit(argv[0]);
	test_builtin_test_argc_rules();
	test_builtin_test_file_primaries();
	test_builtin_test_binary_primaries();
	test_builtin_test_grammar();
	test_builtin_bracket();
	test_builtin_in_compound_contexts(argv[0]);

	if (fails) { printf("sh: failures: %d\n", fails); return 1; }
	printf("sh: all ok (stage 6a: lexer + parser + execution of simple commands, redirections, pipelines, subshells and brace groups, command substitution, and the built-in dispatcher with test/[/:/true/false/exit/cd -- see test/sh-design.md)\n");
	return 0;
}
