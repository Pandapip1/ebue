/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The shell's function table (XCU 2.9.5 "Function Definition Command").
 *
 * A definition is a (name, body-source) pair; the body is kept as source
 * text rather than an AST node because a function outlives the sh_list it
 * was defined in (sh.h, sh_command.func_text has the full argument).
 *
 * A linked list, not a hash table: real autoconf `configure` scripts define
 * a few dozen functions at most, and every lookup already follows a
 * heap-touching wordexp(), so a table would be unmeasured optimization.
 *
 * Redefinition replaces (2.9.5 gives no way to have two functions of one
 * name). The new body is built before the old one is freed, so a failed
 * redefinition leaves the previous definition intact.
 */
#include <string.h>
#include "libc.h"
#include "sh.h"

struct sh_fn {
	char *name;
	char *body;
	struct sh_fn *next;
};

static struct sh_fn *table;

static char *dup_str(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = __malloc(n);
	if (p) memcpy(p, s, n);
	return p;
}

static void free_chain(struct sh_fn *f)
{
	while (f) {
		struct sh_fn *n = f->next;
		__free(f->name);
		__free(f->body);
		__free(f);
		f = n;
	}
}

const char *__sh_func_lookup(const char *name)
{
	struct sh_fn *f;
	for (f = table; f; f = f->next)
		if (strcmp(f->name, name) == 0) return f->body;
	return 0;
}

int __sh_func_define(const char *name, const char *body)
{
	struct sh_fn *f;
	char *nb = dup_str(body);

	if (!nb) return -1;
	for (f = table; f; f = f->next) {
		if (strcmp(f->name, name) == 0) {
			__free(f->body);
			f->body = nb;
			return 0;
		}
	}
	f = __malloc(sizeof *f);
	if (!f) { __free(nb); return -1; }
	f->name = dup_str(name);
	if (!f->name) { __free(nb); __free(f); return -1; }
	f->body = nb;
	f->next = table;
	table = f;
	return 0;
}

/* ---- subshell scoping (XCU 2.12) -------------------------------------
 *
 * A function defined inside "( ... )" or a command substitution must not
 * survive it, so execute.c brackets this table the same way it already
 * brackets `environ`, cwd, and the positional parameters.
 *
 * Take-then-copy shape matches param.c's: the take leaves the live table
 * empty and hands the caller the only pointer to it, so nesting can't alias. */
void __sh_funcs_take(struct sh_funcs *out)
{
	out->head = table;
	table = 0;
}

int __sh_funcs_copy(const struct sh_funcs *src)
{
	struct sh_fn *f, *tail = 0, *nf;
	struct sh_fn *built = 0;

	/* Built onto a local head first so a failure part way through frees only
	 * what this call allocated, leaving the live (post-take, empty) table alone. */
	for (f = src->head; f; f = f->next) {
		nf = __malloc(sizeof *nf);
		if (!nf) { free_chain(built); return -1; }
		nf->name = dup_str(f->name);
		nf->body = dup_str(f->body);
		nf->next = 0;
		if (!nf->name || !nf->body) {
			__free(nf->name); __free(nf->body); __free(nf);
			free_chain(built);
			return -1;
		}
		if (tail) tail->next = nf; else built = nf;
		tail = nf;
	}
	free_chain(table);
	table = built;
	return 0;
}

void __sh_funcs_install(struct sh_funcs *in)
{
	free_chain(table);
	table = in->head;
	in->head = 0;
}

/* Releases a taken table instead of putting it back. Nothing in src/sh/ calls
 * this (a subshell always installs), but test/sh-engine.c runs many independent
 * programs in one process and needs to drop definitions between them. */
void __sh_funcs_free(struct sh_funcs *f)
{
	free_chain(f->head);
	f->head = 0;
}
