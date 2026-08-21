/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The child table: one entry per living-or-unreaped child, holding the
 * pid and the process HANDLE waitpid needs to wait on it and read its
 * exit code.
 *
 * The handle is the only thing that keeps a child reapable.  A pid on
 * Windows is a name for a process *object*, and the object goes away as
 * soon as the last handle to it does; once that happens NtOpenProcess by
 * CLIENT_ID cannot find the pid again (and the number may even be reused
 * for something else).  So a table that can fill up is not a table that
 * merely loses track of extra children -- dropping the handle destroys
 * the only way to ever learn how the child exited.  The table therefore
 * grows on demand instead of overflowing.
 *
 * It starts as a static array, so the common case -- and, importantly,
 * any fork/spawn done before or without the allocator -- never calls
 * malloc at all; only the 257th concurrently-unreaped child does.  Growth
 * allocates from the process heap (src/malloc/malloc.c: RtlAllocateHeap
 * on __peb->ProcessHeap), which is ordinary address space, so the grown
 * table makes the trip through RtlCloneUserProcess with everything else
 * fork() copies -- see fork.c's header comment.  The pointer __children
 * itself is just a global variable, and globals are memory too, so the
 * clone sees the same pointer aimed at its own copy of the same bytes.
 *
 * If growth fails there is nothing better to do than what the fixed table
 * used to do: __child_add returns -1 and the caller closes the handle,
 * losing the child rather than losing the fork.
 *
 * Note that __child_find returns a pointer *into* the table, which a
 * later growth may move.  Every caller uses it and is done with it before
 * any further __child_add, so this is safe as written; anything that
 * wants to hold one across a fork/spawn must remember the pid instead.
 */
#include <string.h>
#include "libc.h"

static struct __child __child_seed[CHILD_MAX_];

struct __child *__children = __child_seed;
int __child_cap = CHILD_MAX_;

/* Refuse to grow past this many entries; a process with a million
 * unreaped children has a leak, not a capacity problem, and the cap
 * keeps the doubling below away from integer overflow. */
#define CHILD_CAP_LIMIT (1 << 20)

static int child_grow(void)
{
	struct __child *n;
	int cap = __child_cap * 2;

	if (__child_cap >= CHILD_CAP_LIMIT) return -1;
	if (cap > CHILD_CAP_LIMIT) cap = CHILD_CAP_LIMIT;
	n = __malloc((size_t)cap * sizeof *n);
	if (!n) return -1;
	memcpy(n, __children, (size_t)__child_cap * sizeof *n);
	memset(n + __child_cap, 0, (size_t)(cap - __child_cap) * sizeof *n);
	if (__children != __child_seed) __free(__children);
	__children = n;
	__child_cap = cap;
	return 0;
}

int __child_add(int pid, HANDLE h)
{
	int i;
	for (;;) {
		for (i = 0; i < __child_cap; i++)
			if (!__children[i].pid) {
				__children[i].pid = pid;
				__children[i].h = h;
				__children[i].done = 0;
				__children[i].status = 0;
				return 0;
			}
		if (child_grow() < 0) return -1;
	}
}

struct __child *__child_find(int pid)
{
	int i;
	for (i = 0; i < __child_cap; i++)
		if (__children[i].pid == pid) return &__children[i];
	return 0;
}

void __child_remove(struct __child *c)
{
	if (c->h) NtClose(c->h);
	c->pid = 0;
	c->h = 0;
}
