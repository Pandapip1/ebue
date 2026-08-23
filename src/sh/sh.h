/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Internal AST and entry points for the in-process POSIX shell (see
 * test/sh-design.md for why this exists and how it links).  Nothing in
 * here is a public interface: it is linked into libc.a and consumed by
 * the other files in this directory, the sh/ binary's main(), and (from
 * stage 5 on) wordexp()/system()/popen().  All names begin with __sh_
 * or SH_.
 *
 * Grammar coverage is the subset test/sh-design.md scopes in: simple
 * commands (including leading NAME=value assignments), pipelines,
 * '&&'/'||'/';'/newline lists, '&' as an asynchronous separator,
 * subshells '( list )' and brace groups '{ list ; }', and redirections
 * including here-documents.  Control-flow reserved words (if/while/for/
 * case), functions, aliases and job control are deliberately out of
 * scope -- see the design note and the top-level task report.
 *
 * '!' pipeline negation is parsed as a reserved word (a bare, unquoted
 * WORD token whose text is exactly "!"/"{"/"}"), recognised only where
 * the grammar expects it.  Every other operator -- | & ; < > ( ) { } --
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

enum sh_cmd_kind { SH_CMD_SIMPLE, SH_CMD_SUBSHELL, SH_CMD_BRACE };

struct sh_command {
	enum sh_cmd_kind kind;

	/* SH_CMD_SIMPLE */
	struct sh_word *assigns;    /* leading NAME=value prefix words */
	struct sh_word *words;      /* command name + arguments */

	/* SH_CMD_SUBSHELL / SH_CMD_BRACE */
	struct sh_list *body;

	/* every kind: redirections attached directly to this command */
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

#endif
