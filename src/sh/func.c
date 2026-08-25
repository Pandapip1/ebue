/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The shell's function table (XCU 2.9.5 "Function Definition Command").
 *
 * A definition is a (name, body-source) pair.  Why the body is source
 * text rather than an AST node is argued where the field is declared --
 * src/sh/sh.h, sh_command.func_text -- and comes down to lifetime: a
 * function outlives the sh_list it was defined in, and this shell frees
 * a complete AST per command substitution and per program.
 *
 * A linked list, not a hash table, and deliberately so: five real
 * autoconf `configure` scripts define between 4 and 30 functions each,
 * and every lookup here happens after a wordexp() that has already
 * touched the heap several times.  A table would be a measurement
 * without a measurement behind it.
 *
 * 2.9.5: "The implementation shall maintain separate name spaces for
 * functions and variables."  That falls out for free here, since this
 * table has nothing to do with `environ`, which is where every variable
 * in this shell lives -- a `PATH` function and a `PATH` variable are
 * unrelated objects, as they must be.
 *
 * Redefinition replaces: 2.9.5 gives no way to have two functions of
 * one name, and every shell takes the later definition.  The new body
 * is built before the old one is released, so a failed redefinition
 * leaves the previous definition intact rather than unsetting it.
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
 * A function defined inside "( ... )" or inside a command substitution
 * must not survive it, exactly as an assignment or a `cd` must not:
 * 2.12's subshell environment is a copy, and "changes made to the
 * subshell environment shall not affect the shell environment".
 * src/sh/execute.c already brackets `environ`, the working directory and
 * the positional parameters; this is the same bracket for one more kind
 * of state.
 *
 * The same take-then-copy shape as src/sh/param.c's, for the same
 * reason: the take leaves the live table empty and hands the caller the
 * only pointer to the outer one, so there is exactly one owner of every
 * node at every moment and nesting cannot alias. */
void __sh_funcs_take(struct sh_funcs *out)
{
	out->head = table;
	table = 0;
}

int __sh_funcs_copy(const struct sh_funcs *src)
{
	struct sh_fn *f, *tail = 0, *nf;
	struct sh_fn *built = 0;

	/* Built in source order onto a local head first, so a failure part
	 * way through frees only what this call allocated and leaves the
	 * live table (empty, post-take) untouched for the caller to
	 * restore. */
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

/* The third operation of the take/copy/install trio: release a taken
 * table instead of putting it back.  A subshell always installs, so
 * nothing in src/sh/ calls this -- but a *take* that is never matched
 * by an install is a leak with no way to clean it up, and the only
 * reason exec.c never does one is that it never wants to.  A caller
 * that does want one (test/sh-engine.c runs many independent programs
 * in a single process, where a real shell process runs exactly one, so
 * it has to be able to drop the definitions between them) has no other
 * way to say so. */
void __sh_funcs_free(struct sh_funcs *f)
{
	free_chain(f->head);
	f->head = 0;
}
