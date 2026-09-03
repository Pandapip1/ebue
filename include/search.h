/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

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

/* lfind can accept an empty array without touching key/base/compar, but
 * always reads nelp to determine whether it is empty. */
void *lsearch(const void *, void *, size_t *, size_t, int (*)(const void *, const void *)) __attribute__((nonnull(1, 2, 3, 5)));
void *lfind(const void *, const void *, size_t *, size_t, int (*)(const void *, const void *)) __attribute__((nonnull(3)));

/* pred is genuinely optional: a null pred makes element "become a
 * one-element circular queue", per POSIX. */
void insque(void *, void *) __attribute__((nonnull(1)));
void remque(void *) __attribute__((nonnull(1)));

#ifdef __cplusplus
}
#endif

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
