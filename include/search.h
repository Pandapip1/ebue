/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _SEARCH_H
#define _SEARCH_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_size_t
#include <bits/alltypes.h>

typedef struct entry {
	char *key;
	void *data;
} ENTRY;

typedef enum { FIND, ENTER } ACTION;
typedef enum { preorder, postorder, endorder, leaf } VISIT;

int hcreate(size_t);
void hdestroy(void);
ENTRY *hsearch(ENTRY, ACTION);

void *tsearch(const void *, void **, int (*)(const void *, const void *));
void *tfind(const void *, void *const *, int (*)(const void *, const void *));
void *tdelete(const void *__restrict, void **__restrict, int (*)(const void *, const void *));
void twalk(const void *, void (*)(const void *, VISIT, int));

/* lfind's nelp is dereferenced unconditionally (`for (i = 0; i <
 * *nelp; ...)`, the loop condition evaluated at least once); key/base
 * are deliberately left unmarked -- src/search/lsearch.c's own NOLINT
 * comment on lsearch() documents that base == NULL with *nelp == 0 is
 * a real, considered case this family does not guard against, not an
 * oversight, so marking base nonnull here would be a false claim.
 * lsearch() forwards straight into lfind() and touches *nelp itself
 * afterward too, inheriting the same requirement on nelp. */
void *lsearch(const void *, void *, size_t *, size_t, int (*)(const void *, const void *)) __attribute__((nonnull(3)));
void *lfind(const void *, const void *, size_t *, size_t, int (*)(const void *, const void *)) __attribute__((nonnull(3)));

/* insque's element is required: src/search/insque.c's body writes
 * through it (`e->fwd = NULL;`) unconditionally, even on the `!p`
 * branch; pred is genuinely optional (insque.html: a null pred makes
 * element "become a one-element circular queue", not an omitted
 * check -- the body's own `if (!p) { ...; return; }` is exactly that
 * documented convention). remque's element is required the same way
 * (`if (e->fwd) ...`, dereferenced first). */
void insque(void *, void *) __attribute__((nonnull(1)));
void remque(void *) __attribute__((nonnull(1)));

#ifdef __cplusplus
}
#endif

#endif
