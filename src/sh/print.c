/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Canonical reprint of the AST sh.h declares. Exists for stage 1's
 * "testable on its own: parse-and-print" requirement -- test/sh-engine.c
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
};

static void queue_heredoc(struct pctx *c, const struct sh_redir *r)
{
	struct hdq *n = __malloc(sizeof *n);
	if (!n) return; /* best-effort: printing is a debug/test aid, never the only copy of the AST */
	n->r = r;
	n->next = 0;
	if (c->tail) c->tail->next = n; else c->head = n;
	c->tail = n;
}

static void drain_heredocs(struct pctx *c)
{
	struct hdq *h = c->head;
	c->head = c->tail = 0;
	while (h) {
		struct hdq *n = h->next;
		if (h->r->heredoc) fputs(h->r->heredoc, c->f);
		fputs(h->r->word, c->f);
		fputc('\n', c->f);
		__free(h);
		h = n;
	}
}

static void print_redir(struct pctx *c, const struct sh_redir *r)
{
	static const char *const opstr[] = {
		"<", ">", ">>", "<&", ">&", "<>", ">|", "<<", "<<-"
	};
	fputc(' ', c->f);
	if (r->fd >= 0) fprintf(c->f, "%d", r->fd);
	fputs(opstr[r->op], c->f);
	fputc(' ', c->f);
	fputs(r->word, c->f);
	if (r->op == SH_R_DLESS || r->op == SH_R_DLESSDASH) queue_heredoc(c, r);
}

static void print_redirs(struct pctx *c, const struct sh_redir *r)
{
	for (; r; r = r->next) print_redir(c, r);
}

static void print_words(struct pctx *c, const struct sh_word *w, int leading_space)
{
	for (; w; w = w->next) {
		if (leading_space) fputc(' ', c->f);
		leading_space = 1;
		fputs(w->text, c->f);
	}
}

static void print_list(struct pctx *c, const struct sh_list *list);

static void print_command(struct pctx *c, const struct sh_command *cmd)
{
	switch (cmd->kind) {
	case SH_CMD_SUBSHELL:
		fputc('(', c->f);
		print_list(c, cmd->body);
		fputc(')', c->f);
		break;
	case SH_CMD_BRACE:
		fputs("{ ", c->f);
		print_list(c, cmd->body);
		fputs("}", c->f);
		break;
	default:
		print_words(c, cmd->assigns, 0);
		print_words(c, cmd->words, cmd->assigns != 0);
		break;
	}
	print_redirs(c, cmd->redirs);
}

static void print_pipeline(struct pctx *c, const struct sh_pipeline *pl)
{
	size_t i;
	if (pl->bang) fputs("! ", c->f);
	for (i = 0; i < pl->ncommands; i++) {
		if (i) fputs(" | ", c->f);
		print_command(c, &pl->commands[i]);
	}
}

static void print_andor(struct pctx *c, const struct sh_andor *a)
{
	for (; a; a = a->next) {
		if (a->op == SH_AO_AND) fputs(" && ", c->f);
		else if (a->op == SH_AO_OR) fputs(" || ", c->f);
		print_pipeline(c, &a->pipeline);
	}
}

static void print_list(struct pctx *c, const struct sh_list *list)
{
	const struct sh_list_item *it;
	if (!list) return;
	for (it = list->items; it; it = it->next) {
		print_andor(c, it->andor);
		if (it->sep == SH_SEP_AMP) fputs(" &", c->f);
		fputc('\n', c->f);
		drain_heredocs(c);
	}
}

void __sh_print_list(FILE *f, const struct sh_list *list)
{
	struct pctx c;
	c.f = f;
	c.head = c.tail = 0;
	print_list(&c, list);
}
