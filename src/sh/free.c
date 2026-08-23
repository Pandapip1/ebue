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
		__free(r);
		r = n;
	}
}

void __sh_free_command_contents(struct sh_command *c)
{
	__sh_free_words(c->assigns);
	__sh_free_words(c->words);
	__sh_free_redirs(c->redirs);
	__sh_list_free(c->body);
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
