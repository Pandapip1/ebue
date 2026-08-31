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

/* lsearch's key, base, nelp, and compar are required: a miss appends key
 * through base and the scan invokes compar whenever the array is nonempty.
 * lfind can accept an empty array without touching key/base/compar, but
 * always reads nelp to determine whether it is empty. */
void *lsearch(const void *, void *, size_t *, size_t, int (*)(const void *, const void *)) __attribute__((nonnull(1, 2, 3, 5)));
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
