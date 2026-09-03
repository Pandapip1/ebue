/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Canonical reprint of the AST sh.h declares. Exists so the parser is
 * testable on its own, via parse-and-print -- test/sh-engine.c
 * round-trips parse() -> print() -> parse() -> print() and checks the
 * second print is a fixed point, which exercises every AST field
 * without needing a second, hand-written AST-equality walk.
 *
 * Separators are canonicalised: every list item ends with a real
 * newline (a bare newline is exactly as valid a separator as ';' --
 * XCU Grammar's `separator`), which is also what lets a here-document
 * body be reprinted immediately after it, mirroring how parse.c's
 * lexer drains pending here-documents on the newline that ends the
 * line containing '<<'/'<<-'.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <string.h>
#include "libc.h"
#include "sh.h"

struct hdq {
	const struct sh_redir *r;
	struct hdq *next;
};

struct pctx {
	FILE *f;
	struct hdq *head, *tail;
	int failed;
};

static void emit_char(struct pctx *c, int ch)
{
	if (!c->failed && fputc(ch, c->f) == EOF) c->failed = 1;
}

static void emit_string(struct pctx *c, const char *s)
{
	if (!c->failed && fputs(s, c->f) < 0) c->failed = 1;
}

static void emit_fd(struct pctx *c, int fd)
{
	if (!c->failed && fprintf(c->f, "%d", fd) < 0) c->failed = 1;
}

/* c is required: `c->tail`/`c->head` are dereferenced directly once
 * `n` (the freshly __malloc'd queue node) is non-NULL, and every real
 * call site passes the address of a real pctx. r is left unmarked --
 * only ever stored as a raw pointer value (`n->r = r;`), never
 * dereferenced by this function itself. */
static void queue_heredoc(struct pctx *c, const struct sh_redir *r) __attribute__((nonnull(1)));
static void queue_heredoc(struct pctx *c, const struct sh_redir *r)
{
	struct hdq *n = __malloc(sizeof *n);
	if (!n) { c->failed = 1; return; }
	n->r = r;
	n->next = 0;
	if (c->tail) c->tail->next = n; else c->head = n;
	c->tail = n;
}

static void queue_nested_heredocs_list(struct pctx *, const struct sh_list *);

/* Function definitions are printed from func_text, but parse.c may retain
 * their body AST when a here-document was still pending at the end of the
 * definition.  Walk that retained tree in source order so the bodies are
 * emitted after the definition's terminating newline just like any other
 * queued here-document. */
/* cmd is required: `switch (cmd->kind)` is this function's first
 * statement. c is left unmarked -- only ever forwarded into
 * queue_nested_heredocs_list()/queue_heredoc(), never dereferenced by
 * this function itself. */
// NOLINTNEXTLINE(misc-no-recursion) -- formatting and heredoc traversal mirror the nested shell-AST hierarchy
static void queue_nested_heredocs_command(struct pctx *c,
		const struct sh_command *cmd) __attribute__((nonnull(2)));
// NOLINTNEXTLINE(misc-no-recursion) -- formatting and heredoc traversal mirror the nested shell-AST hierarchy
static void queue_nested_heredocs_command(struct pctx *c,
		const struct sh_command *cmd)
{
	const struct sh_ifarm *arm;
	const struct sh_redir *r;

	switch (cmd->kind) {
	case SH_CMD_SUBSHELL:
	case SH_CMD_BRACE:
		queue_nested_heredocs_list(c, cmd->u.group.body);
		break;
	case SH_CMD_IF:
		for (arm = cmd->u.ifcmd.arms; arm; arm = arm->next) {
			queue_nested_heredocs_list(c, arm->cond);
			queue_nested_heredocs_list(c, arm->body);
		}
		queue_nested_heredocs_list(c, cmd->u.ifcmd.else_body);
		break;
	case SH_CMD_LOOP:
		queue_nested_heredocs_list(c, cmd->u.loop.cond);
		queue_nested_heredocs_list(c, cmd->u.loop.body);
		break;
	case SH_CMD_FOR:
		queue_nested_heredocs_list(c, cmd->u.forloop.body);
		break;
	case SH_CMD_FUNCDEF:
		if (cmd->u.funcdef.func_body)
			queue_nested_heredocs_command(c, cmd->u.funcdef.func_body);
		break;
	default:
		break;
	}
	for (r = cmd->redirs; r; r = r->next)
		if (r->op == SH_R_DLESS || r->op == SH_R_DLESSDASH)
			queue_heredoc(c, r);
}

/* Neither parameter is marked here: list is genuinely optional --
 * `if (!list) return;` right below is a real, working check, exercised
 * whenever a compound command's optional part (e.g. an `if` with no
 * `else`) is absent -- and c is forward-only, never dereferenced
 * directly by this function itself.
 *
 * Not fixed by this: the flagged `andor->pipeline.commands[i]` deref is
 * about `andor`, a local loop variable walking `item->andor`, and its
 * own `.pipeline.commands` array pointer -- an internal AST invariant
 * neither parameter here can express via `nonnull`, the same class of
 * residual as execute.c's we.we_wordv[0]/__environ[i]. */
// NOLINTNEXTLINE(misc-no-recursion) -- formatting and heredoc traversal mirror the nested shell-AST hierarchy
static void queue_nested_heredocs_list(struct pctx *c,
		const struct sh_list *list)
{
	const struct sh_list_item *item;
	const struct sh_andor *andor;
	size_t i;

	if (!list) return;
	for (item = list->items; item; item = item->next)
		for (andor = item->andor; andor; andor = andor->next)
			for (i = 0; i < andor->pipeline.ncommands; i++)
				queue_nested_heredocs_command(c,
				    &andor->pipeline.commands[i]);
}

/* c is required: `struct hdq *h = c->head;` is this function's first
 * statement. __sh_print_list() below is the only real entry point,
 * always via `&c` where c is its own on-stack pctx.
 *
 * Not fixed by this: the flagged `h->r->heredoc` deref is about `h->r`,
 * set by queue_heredoc() elsewhere (always a real redir there, but not
 * an invariant this function's own parameter can express), not about c. */
static void drain_heredocs(struct pctx *c) __attribute__((nonnull(1)));
static void drain_heredocs(struct pctx *c)
{
	struct hdq *h = c->head;
	c->head = c->tail = 0;
	while (h) {
		struct hdq *n = h->next;
		if (h->r->heredoc) emit_string(c, h->r->heredoc);
		emit_string(c, h->r->heredoc_delim ? h->r->heredoc_delim : h->r->word);
		emit_char(c, '\n');
		__free(h);
		h = n;
	}
}

/* Both required: `emit_char(c, ' ');` is this function's first statement,
 * and `if (r->fd >= 0)` right after it is equally unconditional, with
 * no branch between them. print_redirs() below (the only caller)
 * always passes a real c and a real list node. */
static void print_redir(struct pctx *c, const struct sh_redir *r)
    __attribute__((nonnull(1, 2)));
static void print_redir(struct pctx *c, const struct sh_redir *r)
{
	static const char *const opstr[] = {
		"<", ">", ">>", "<&", ">&", "<>", ">|", "<<", "<<-"
	};
	emit_char(c, ' ');
	if (r->fd >= 0) emit_fd(c, r->fd);
	emit_string(c, opstr[r->op]);
	emit_char(c, ' ');
	emit_string(c, r->word);
	if (r->op == SH_R_DLESS || r->op == SH_R_DLESSDASH) queue_heredoc(c, r);
}

static void print_redirs(struct pctx *c, const struct sh_redir *r)
{
	for (; r; r = r->next) print_redir(c, r);
}

/* c is required: whenever the loop body runs at all, `c->f` is
 * dereferenced with no NULL check, and every real call site passes a
 * real pctx. w is deliberately left unmarked -- `for (; w; w = w->next)`
 * is the usual NULL-safe "empty list" walk, and a command with no words
 * (e.g. an assignment-only simple command) genuinely passes NULL here. */
static void print_words(struct pctx *c, const struct sh_word *w, int leading_space)
    __attribute__((nonnull(1)));
static void print_words(struct pctx *c, const struct sh_word *w, int leading_space)
{
	for (; w; w = w->next) {
		if (leading_space) emit_char(c, ' ');
		leading_space = 1;
		if (!strcmp(w->text, "!")) emit_string(c, "'!'");
		else emit_string(c, w->text);
	}
}

static void print_list(struct pctx *c, const struct sh_list *list);

/* cmd is required: `switch (cmd->kind)` is this function's first
 * statement. c is required too: most switch arms directly dereference
 * `c->f` (fputc()/fputs() calls), and print_list()/print_pipeline()
 * below always pass a real pctx. */
// NOLINTNEXTLINE(misc-no-recursion) -- formatting and heredoc traversal mirror the nested shell-AST hierarchy
static void print_command(struct pctx *c, const struct sh_command *cmd)
    __attribute__((nonnull(1, 2)));
// NOLINTNEXTLINE(misc-no-recursion) -- formatting and heredoc traversal mirror the nested shell-AST hierarchy
static void print_command(struct pctx *c, const struct sh_command *cmd)
{
	switch (cmd->kind) {
	case SH_CMD_SUBSHELL:
		emit_char(c, '(');
		print_list(c, cmd->u.group.body);
		emit_char(c, ')');
		break;
	case SH_CMD_BRACE:
		emit_string(c, "{ ");
		print_list(c, cmd->u.group.body);
		emit_string(c, "}");
		break;
	/* The compound commands are reprinted in the multi-line form XCU
	 * 2.9.4 gives them, with a real <newline> before each terminator
	 * reserved word rather than a "; ".  That is not cosmetic: print_list()
	 * already ends every item with a newline, so `fi`/`done` land in
	 * command position on a fresh line, which is the only position
	 * parse.c recognises a reserved word in -- printing "cmd; fi" would
	 * reparse `fi` as an argument and break the round-trip the whole
	 * file exists to support. */
	case SH_CMD_IF: {
		const struct sh_ifarm *a;
		for (a = cmd->u.ifcmd.arms; a; a = a->next) {
			emit_string(c, a == cmd->u.ifcmd.arms ? "if " : "elif ");
			print_list(c, a->cond);
			emit_string(c, "then\n");
			print_list(c, a->body);
		}
		if (cmd->u.ifcmd.else_body) {
			emit_string(c, "else\n");
			print_list(c, cmd->u.ifcmd.else_body);
		}
		emit_string(c, "fi");
		break;
	}
	case SH_CMD_LOOP:
		emit_string(c, cmd->u.loop.until ? "until " : "while ");
		print_list(c, cmd->u.loop.cond);
		emit_string(c, "do\n");
		print_list(c, cmd->u.loop.body);
		emit_string(c, "done");
		break;
	case SH_CMD_FOR:
		emit_string(c, "for ");
		emit_string(c, cmd->u.forloop.name);
		if (cmd->u.forloop.have_in) {
			emit_string(c, " in");
			print_words(c, cmd->u.forloop.words, 1);
		}
		emit_string(c, "\ndo\n");
		print_list(c, cmd->u.forloop.body);
		emit_string(c, "done");
		break;
	/* XCU 2.9.5: "fname ( ) compound-command [io-redirect...]".  The
	 * body is reprinted as the raw source text the parser captured
	 * (sh.h's func_text), which is what makes the round-trip a fixed
	 * point: re-parsing this output captures the identical substring,
	 * so the second print is byte-for-byte the first.  A canonicalised
	 * reprint of a body this file never parsed could not promise
	 * that. */
	case SH_CMD_FUNCDEF:
		emit_string(c, cmd->u.funcdef.name);
		emit_string(c, "() ");
		emit_string(c, cmd->u.funcdef.func_text);
		if (cmd->u.funcdef.func_body)
			queue_nested_heredocs_command(c, cmd->u.funcdef.func_body);
		break;
	default:
		print_words(c, cmd->u.simple.assigns, 0);
		print_words(c, cmd->u.simple.words, cmd->u.simple.assigns != 0);
		break;
	}
	print_redirs(c, cmd->redirs);
}

/* pl is required: `if (pl->bang)` is this function's first statement.
 * c is required too: reached directly (`emit_string(c, " ! ")`) on the
 * real, reachable `pl->bang` path and the real, reachable `i > 0`
 * path of a multi-command pipeline, and print_andor() below always
 * passes a real pctx and `&a->pipeline`. */
// NOLINTNEXTLINE(misc-no-recursion) -- formatting and heredoc traversal mirror the nested shell-AST hierarchy
static void print_pipeline(struct pctx *c, const struct sh_pipeline *pl)
    __attribute__((nonnull(1, 2)));
// NOLINTNEXTLINE(misc-no-recursion) -- formatting and heredoc traversal mirror the nested shell-AST hierarchy
static void print_pipeline(struct pctx *c, const struct sh_pipeline *pl)
{
	size_t i;
	if (pl->bang) emit_string(c, "! ");
	for (i = 0; i < pl->ncommands; i++) {
		if (i) emit_string(c, " | ");
		print_command(c, &pl->commands[i]);
	}
}

// NOLINTNEXTLINE(misc-no-recursion) -- formatting and heredoc traversal mirror the nested shell-AST hierarchy
static void print_andor(struct pctx *c, const struct sh_andor *a)
{
	for (; a; a = a->next) {
		if (a->op == SH_AO_AND) emit_string(c, " && ");
		else if (a->op == SH_AO_OR) emit_string(c, " || ");
		print_pipeline(c, &a->pipeline);
	}
}

// NOLINTNEXTLINE(misc-no-recursion) -- formatting and heredoc traversal mirror the nested shell-AST hierarchy
static void print_list(struct pctx *c, const struct sh_list *list)
{
	const struct sh_list_item *it;
	if (!list) return;
	for (it = list->items; it; it = it->next) {
		print_andor(c, it->andor);
		if (it->sep == SH_SEP_AMP) emit_string(c, " &");
		emit_char(c, '\n');
		drain_heredocs(c);
	}
}

int __sh_print_list(FILE *f, const struct sh_list *list)
{
	struct pctx c;
	c.f = f;
	c.head = c.tail = 0;
	c.failed = 0;
	print_list(&c, list);
	return c.failed ? -1 : 0;
}

// NOLINTEND(misc-include-cleaner)
