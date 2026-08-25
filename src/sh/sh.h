/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Internal AST and entry points for the in-process POSIX shell (see
 * test/sh-design.md for why this exists and how it links).  Nothing in
 * here is a public interface: it is linked into libc.a and consumed by
 * the other files in this directory, the sh/ binary's main()
 * (sh/main.c), and -- as of stage 5, through the single __sh_cmdsub()
 * call-out declared in src/internal/libc.h -- wordexp()'s command
 * substitution.  system() and popen() still hand their command string
 * to %ComSpec%/cmd.exe and are test/sh-design.md's item 5, not this
 * stage's.  All names begin with __sh_ or SH_.
 *
 * Grammar coverage is the subset test/sh-design.md scopes in: simple
 * commands (including leading NAME=value assignments), pipelines,
 * '&&'/'||'/';'/newline lists, '&' as an asynchronous separator,
 * subshells '( list )' and brace groups '{ list ; }', redirections
 * including here-documents, (stage 5) command substitution in both the
 * '$(...)' and the '`...`' form, and (stage 6b) the compound commands
 * 'if/then/elif/else/fi', 'while|until ... do ... done' and
 * 'for name [in words] do ... done' (XCU 2.9.4).
 *
 * Still out of scope, and still WORD tokens here: 'case', function
 * definitions, aliases and job control.  Because each of those lexes as
 * an ordinary WORD, a program using one would otherwise be *executed*
 * as something else entirely (an external command called "case");
 * sh/main.c refuses such a program up front, with a diagnostic naming
 * what is unsupported, rather than letting it run -- see that file's
 * header for the full list and why refusing beats a misleading
 * "command not found".  'for name' with no 'in' list is refused the
 * same way, because 2.9.4 defines it as 'in "$@"' and there are no
 * positional parameters.
 *
 * '!' pipeline negation is parsed as a reserved word (a bare, unquoted
 * WORD token whose text is exactly "!"/"{"/"}"), as are the compound
 * commands' own reserved words, all by the same rule (XCU 2.10.1 rule
 * 1) and recognised only where the grammar expects them; a misplaced
 * one -- a bare 'fi', a stray 'do' -- is a syntax error rather than a
 * command of that name.  Every other operator -- | & ; < > ( ) { } --
 * is tokenised as a true lexer-level operator and can therefore never
 * appear inside an unquoted WORD; this matches src/wordexp/wordexp.c,
 * which already rejects all of these as WRDE_BADCHAR when unquoted, so
 * the two layers agree on what a "word" is.
 */
#ifndef NTLIBC_SH_H
#define NTLIBC_SH_H

#include <stddef.h>
#include <stdio.h>

/* ---- words --------------------------------------------------------------
 * `text` is the raw source text of the word: quotes and backslashes are
 * kept exactly as written.  Expansion (parameter/tilde/pathname) and
 * quote removal happen later, at execution time, via the shared
 * routines factored out of src/wordexp/wordexp.c (see __sh_expand.h) --
 * the parser's only job is to find word boundaries correctly. */
struct sh_word {
	char *text;
	struct sh_word *next;
};

enum sh_redir_op {
	SH_R_LESS,       /* <  */
	SH_R_GREAT,      /* >  */
	SH_R_DGREAT,     /* >> */
	SH_R_LESSAND,    /* <& */
	SH_R_GREATAND,   /* >& */
	SH_R_LESSGREAT,  /* <> */
	SH_R_CLOBBER,    /* >| */
	SH_R_DLESS,      /* << */
	SH_R_DLESSDASH   /* <<- */
};

struct sh_redir {
	enum sh_redir_op op;
	int fd;              /* explicit io_number, or -1 for the operator's default */
	char *word;           /* target word / heredoc delimiter, raw text */
	char *heredoc;         /* SH_R_DLESS(DASH) only: the literal body text */
	int heredoc_quoted;     /* SH_R_DLESS(DASH) only: delimiter was quoted -> body gets no expansion */
	struct sh_redir *next;
};

enum sh_cmd_kind {
	SH_CMD_SIMPLE,
	SH_CMD_SUBSHELL,
	SH_CMD_BRACE,
	SH_CMD_IF,     /* if/elif/else/fi     (XCU 2.9.4 "The if Conditional Construct") */
	SH_CMD_LOOP,   /* while/until/do/done (XCU 2.9.4 "The while Loop"/"The until Loop") */
	SH_CMD_FOR     /* for/in/do/done      (XCU 2.9.4 "The for Loop") */
};

/* One arm of an if command: the `if`/`elif` condition and the
 * `then` compound-list it guards.  2.9.4 gives `elif` exactly the same
 * shape as `if` ("each elif compound-list shall be executed, in turn,
 * and if its exit status is zero, the then compound-list shall be
 * executed"), so they are one node type in a list rather than a
 * separate case -- the `else` part is the only genuinely different
 * thing, and it hangs off the command instead. */
struct sh_ifarm {
	struct sh_list *cond;
	struct sh_list *body;
	struct sh_ifarm *next;
};

struct sh_command {
	enum sh_cmd_kind kind;

	/* SH_CMD_SIMPLE */
	struct sh_word *assigns;    /* leading NAME=value prefix words */
	/* SH_CMD_SIMPLE: command name + arguments.
	 * SH_CMD_FOR: the `in` word list, raw and unexpanded -- 2.9.4 says
	 * "the list of words following in shall be expanded to generate a
	 * list of items", i.e. at execution time, exactly like a simple
	 * command's arguments, so they are the same kind of thing and get
	 * the same field. */
	struct sh_word *words;

	/* SH_CMD_SUBSHELL / SH_CMD_BRACE: the group's body.
	 * SH_CMD_LOOP / SH_CMD_FOR: the `do ... done` compound-list. */
	struct sh_list *body;

	/* SH_CMD_IF */
	struct sh_ifarm *arms;      /* the if arm, then each elif arm */
	struct sh_list *else_body;  /* the `else` part, or NULL */

	/* SH_CMD_LOOP */
	struct sh_list *cond;       /* compound-list-1 */
	int until;                  /* 0: `while`, 1: `until` */

	/* SH_CMD_FOR */
	char *name;                 /* the NAME between `for` and `in` */
	int have_in;                /* 0: `for name` with no `in` word list,
	                             * which 2.9.4 defines as `in "$@"` -- see
	                             * exec.c and sh/main.c on why that is
	                             * refused rather than approximated */

	/* every kind: redirections attached directly to this command.
	 * 2.9.4: "each can be followed by redirections on the same line as
	 * the terminator.  Each redirection shall apply to all the commands
	 * within the compound command that do not explicitly override that
	 * redirection." */
	struct sh_redir *redirs;
};

struct sh_pipeline {
	int bang;                     /* leading '!' */
	struct sh_command *commands;  /* array of ncommands */
	size_t ncommands;
};

enum sh_andor_op { SH_AO_NONE, SH_AO_AND, SH_AO_OR };

/* One term of an and-or list, e.g. "a && b || c" is three sh_andor
 * nodes: {a,NONE} -> {b,AND} -> {c,OR}, each op naming the operator
 * that joined *this* pipeline to the one before it. */
struct sh_andor {
	struct sh_pipeline pipeline;
	enum sh_andor_op op;
	struct sh_andor *next;
};

enum sh_sep {
	SH_SEP_SEQ,   /* ';' or a bare newline: run sequentially */
	SH_SEP_AMP,   /* '&': run asynchronously */
	SH_SEP_END    /* no separator: end of the list */
};

struct sh_list_item {
	struct sh_andor *andor;   /* linked list of pipelines, see sh_andor */
	enum sh_sep sep;
	struct sh_list_item *next;
};

struct sh_list {
	struct sh_list_item *items;
};

/* Parses `src` (the whole program text, e.g. sh -c's operand) as a
 * complete_command (XCU Grammar). On success returns a freshly
 * __malloc'd AST (free with __sh_list_free). On a syntax error returns
 * NULL and, if errbuf is non-NULL, writes a NUL-terminated diagnostic
 * (truncated to fit) into errbuf[0..errbuflen). An empty or all-blank/
 * all-comment `src` is not an error: it returns a non-NULL list with a
 * NULL items pointer. */
struct sh_list *__sh_parse(const char *src, char *errbuf, size_t errbuflen);

void __sh_list_free(struct sh_list *list);

/* Shared by __sh_list_free() and parse.c's parse-error cleanup paths. */
void __sh_free_words(struct sh_word *w);
void __sh_free_redirs(struct sh_redir *r);
/* Frees everything a sh_command *owns* (words/assigns/redirs/body), but
 * not the struct itself -- callers that store sh_command by value in an
 * array (see sh_pipeline.commands) free the array separately. */
void __sh_free_command_contents(struct sh_command *c);

/* Reprints `list` in a canonical, re-parseable form. Used by stage 1's
 * parse-and-print tests and (harmlessly) available to anything that
 * wants to log a parsed command. Never fails; short of a write error on
 * `f` it always terminates. */
void __sh_print_list(FILE *f, const struct sh_list *list);

/* ---- execution (stage 2 on -- see src/sh/exec.c) --------------------
 *
 * Each of these returns 0 and writes an exit status through `status`
 * on success, or -1 (status left untouched) for a construct this
 * stage's executor does not implement yet -- see exec.c's header
 * comment for exactly which. Callers must not treat -1 as "exit status
 * -1"; it means "cannot execute this AST node at all right now".
 *
 * The command-substitution entry point wordexp() calls is deliberately
 * NOT here: it is declared in src/internal/libc.h, which is where a
 * declaration shared between source directories belongs. See
 * __sh_cmdsub() there and its implementation in exec.c. */
/* ---- built-in utilities (XCU 1.6, 2.9.1, 2.14) ----------------------
 *
 * src/sh/builtin.c owns the table and every implementation; exec.c
 * consults it from the one place XCU 2.9.1 says a command name is
 * decided -- after the word expansions, on the expanded argv, before
 * any PATH search.  See builtin.c's header comment for why `test`,
 * `:`, `true`, `false` and `exit` are built in here rather than left to
 * an honest "command not found". */
struct sh_builtin_ctx {
	int argc;             /* expanded argv count, argv[0] is the name */
	char **argv;
	int env_mutate;       /* 0: this invocation's effect on the shell
	                       * execution environment (XCU 2.12) is going
	                       * to be discarded -- e.g. one stage of a
	                       * multi-command pipeline */
	int last_status;      /* the status of the last command executed,
	                       * i.e. what `exit` with no operand uses */
	int status;           /* out: the utility's exit status */
};

struct sh_builtin {
	const char *name;
	int special;          /* XCU 2.14 special built-in (2.8.1 hangs
	                       * error consequences off this) */
	int env_effect;       /* changes something XCU 2.12 lists as part
	                       * of the shell execution environment */
	int (*fn)(struct sh_builtin_ctx *ctx);
};

const struct sh_builtin *__sh_builtin_lookup(const char *name);

/* ---- shell-wide control flow ----------------------------------------
 *
 * `exit` (XCU 2.14) has to unwind out of however many nested lists,
 * and-or terms and pipelines it is buried in, without any of them
 * mistaking the unwind for a command that failed.  A single pending
 * flag, checked by __sh_exec_list()/__sh_exec_andor() between items,
 * does that: they stop iterating and return normally with the status
 * already in *status.  A subshell environment (a "( list )", a command
 * substitution, or a pipeline stage) consumes the pending exit instead
 * of propagating it, because that is what exiting *that* subshell
 * means.  Implemented in exec.c, which is what drives execution. */
void __sh_flow_exit(int status);
int __sh_flow_pending(void);
void __sh_flow_clear(void);

/* The status of the last command executed (XCU 2.8.2), maintained by
 * exec.c and read by `exit` with no operand. */
int __sh_last_status(void);

int __sh_exec_command(const struct sh_command *cmd, int *status);
int __sh_exec_pipeline(const struct sh_pipeline *pl, int *status);
int __sh_exec_andor(const struct sh_andor *a, int *status);
int __sh_exec_list(const struct sh_list *list, int *status);

#endif
