/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Freeing the AST sh.h declares. One recursive walk per node kind;
 * parse.c's error-recovery paths reuse the word/redir/command-contents
 * pieces directly instead of duplicating this traversal.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
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
// NOLINTNEXTLINE(misc-no-recursion) -- the destructor mirrors the owned shell-AST hierarchy
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

/* Switched on c->kind rather than freeing every field unconditionally:
 * the union means only the active variant's pointers are meaningful,
 * and reading a different variant's pointer out of the same bytes is
 * not "freeing an unused NULL" the way it was when every kind had its
 * own separate field.  This still handles a node abandoned mid-parse
 * safely -- new_command() zeroes the whole union up front, and every
 * place parse.c later changes c->kind in place (SUBSHELL/BRACE/FUNCDEF)
 * does so before writing any field of the new variant, so whichever
 * variant c->kind names at the point of a free is the one that has
 * actually been initialised, whether or not parsing went on to finish
 * populating it. */
/* c is required, unlike every free_*() sibling above and below it: this
 * frees a command's *contents*, not a linked-list node, so there is no
 * "NULL means empty list" reading available here the way there is for
 * __sh_free_words()/__sh_free_redirs()/free_ifarms()/__sh_list_free().
 * `switch (c->kind)` dereferences c unconditionally on entry, and every
 * real call site -- this file's own recursive func_body call (guarded
 * by `if (c->u.funcdef.func_body)` first), and free_pipeline_contents()
 * below (always `&pl->commands[i]`, an array element's address) --
 * always passes a real struct. */
// NOLINTNEXTLINE(misc-no-recursion) -- the destructor mirrors the owned shell-AST hierarchy
void __sh_free_command_contents(struct sh_command *c)
{
	__sh_free_redirs(c->redirs);
	switch (c->kind) {
	case SH_CMD_SIMPLE:
		__sh_free_words(c->u.simple.assigns);
		__sh_free_words(c->u.simple.words);
		break;
	case SH_CMD_SUBSHELL:
	case SH_CMD_BRACE:
		__sh_list_free(c->u.group.body);
		break;
	case SH_CMD_IF:
		free_ifarms(c->u.ifcmd.arms);
		__sh_list_free(c->u.ifcmd.else_body);
		break;
	case SH_CMD_LOOP:
		__sh_list_free(c->u.loop.cond);
		__sh_list_free(c->u.loop.body);
		break;
	case SH_CMD_FOR:
		__free(c->u.forloop.name);
		__sh_free_words(c->u.forloop.words);
		__sh_list_free(c->u.forloop.body);
		break;
	case SH_CMD_FUNCDEF:
		__free(c->u.funcdef.name);
		__free(c->u.funcdef.func_text);
		/* Recursive rather than a __sh_list_free() like the fields
		 * above: sh.h's func_body is a bare sh_command, not a list, and
		 * parse.c's free_command() -- which would be the natural call --
		 * is static there.  This is the same two lines it is. */
		if (c->u.funcdef.func_body) {
			__sh_free_command_contents(c->u.funcdef.func_body);
			__free(c->u.funcdef.func_body);
		}
		break;
	}
}

static void free_pipeline_contents(struct sh_pipeline *pl) __attribute__((nonnull(1)));
// NOLINTNEXTLINE(misc-no-recursion) -- the destructor mirrors the owned shell-AST hierarchy
static void free_pipeline_contents(struct sh_pipeline *pl)
{
	size_t i;
	for (i = 0; i < pl->ncommands; i++) __sh_free_command_contents(&pl->commands[i]);
	__free(pl->commands);
}

// NOLINTNEXTLINE(misc-no-recursion) -- the destructor mirrors the owned shell-AST hierarchy
static void free_andor(struct sh_andor *a)
{
	while (a) {
		struct sh_andor *n = a->next;
		free_pipeline_contents(&a->pipeline);
		__free(a);
		a = n;
	}
}

// NOLINTNEXTLINE(misc-no-recursion) -- the destructor mirrors the owned shell-AST hierarchy
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

// NOLINTEND(misc-include-cleaner)
