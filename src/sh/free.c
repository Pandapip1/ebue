/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Freeing the AST sh.h declares. One recursive walk per node kind;
 * parse.c's error-recovery paths reuse the word/redir/command-contents
 * pieces directly instead of duplicating this traversal.
 */
#include "libc.h"
#include "sh.h"

void __sh_free_words(struct sh_word *w)
{
	while (w) {
		struct sh_word *n = w->next;
		__free(w->text);
		__free(w);
		w = n;
	}
}

void __sh_free_redirs(struct sh_redir *r)
{
	while (r) {
		struct sh_redir *n = r->next;
		__free(r->word);
		__free(r->heredoc);
		__free(r->heredoc_delim);
		__free(r);
		r = n;
	}
}

/* SH_CMD_IF's arms.  Each arm owns its two compound-lists; the chain
 * covers the `if` arm and every `elif` arm, which sh.h makes the same
 * node type for exactly this reason. */
static void free_ifarms(struct sh_ifarm *a)
{
	while (a) {
		struct sh_ifarm *n = a->next;
		__sh_list_free(a->cond);
		__sh_list_free(a->body);
		__free(a);
		a = n;
	}
}

/* Unconditional in every field, not switched on c->kind: parse.c's
 * new_command() zeroes all of them for every kind, so a field this
 * command does not use is a NULL that each of these already ignores.
 * Switching on the kind instead would mean a node abandoned halfway
 * through parsing -- kind already set, fields belonging to a different
 * kind still populated by an earlier goto -- leaked whatever the switch
 * decided not to look at. */
void __sh_free_command_contents(struct sh_command *c)
{
	__sh_free_words(c->assigns);
	__sh_free_words(c->words);
	__sh_free_redirs(c->redirs);
	__sh_list_free(c->body);
	free_ifarms(c->arms);
	__sh_list_free(c->else_body);
	__sh_list_free(c->cond);
	__free(c->name);
	__free(c->func_text);
	/* Recursive rather than a __sh_list_free() like the fields above:
	 * sh.h's func_body is a bare sh_command, not a list, and parse.c's
	 * free_command() -- which would be the natural call -- is static
	 * there.  This is the same two lines it is. */
	if (c->func_body) {
		__sh_free_command_contents(c->func_body);
		__free(c->func_body);
	}
}

static void free_pipeline_contents(struct sh_pipeline *pl)
{
	size_t i;
	for (i = 0; i < pl->ncommands; i++) __sh_free_command_contents(&pl->commands[i]);
	__free(pl->commands);
}

static void free_andor(struct sh_andor *a)
{
	while (a) {
		struct sh_andor *n = a->next;
		free_pipeline_contents(&a->pipeline);
		__free(a);
		a = n;
	}
}

void __sh_list_free(struct sh_list *list)
{
	struct sh_list_item *it;
	if (!list) return;
	it = list->items;
	while (it) {
		struct sh_list_item *n = it->next;
		free_andor(it->andor);
		__free(it);
		it = n;
	}
	__free(list);
}
