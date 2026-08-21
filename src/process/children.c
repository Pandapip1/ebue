/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "libc.h"

struct __child __children[CHILD_MAX_];

int __child_add(int pid, HANDLE h)
{
	int i;
	for (i = 0; i < CHILD_MAX_; i++)
		if (!__children[i].pid) {
			__children[i].pid = pid;
			__children[i].h = h;
			__children[i].done = 0;
			__children[i].status = 0;
			return 0;
		}
	return -1;
}

struct __child *__child_find(int pid)
{
	int i;
	for (i = 0; i < CHILD_MAX_; i++)
		if (__children[i].pid == pid) return &__children[i];
	return 0;
}

void __child_remove(struct __child *c)
{
	if (c->h) NtClose(c->h);
	c->pid = 0;
	c->h = 0;
}
