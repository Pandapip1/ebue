/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * insque/remque: the first two members of the caller's structure are
 * the forward and backward links (insque.html DESCRIPTION); handled
 * generically here via a struct whose layout matches that contract.
 */
#include <search.h>
#include <stddef.h>

struct qnode {
	struct qnode *fwd, *bwd;
};

void insque(void *element, void *pred)
{
	struct qnode *e = element, *p = pred;

	if (!p) {
		e->fwd = NULL;
		e->bwd = NULL;
		return;
	}

	e->fwd = p->fwd;
	e->bwd = p;
	if (p->fwd) p->fwd->bwd = e;
	p->fwd = e;
}

void remque(void *element)
{
	struct qnode *e = element;

	if (e->fwd) e->fwd->bwd = e->bwd;
	if (e->bwd) e->bwd->fwd = e->fwd;
}
