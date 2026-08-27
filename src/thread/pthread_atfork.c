/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include "pthread_impl.h"

struct atfork_handler {
	void (*prepare)(void);
	void (*parent)(void);
	void (*child)(void);
};

static struct atfork_handler *handlers;
static size_t handler_count;
static size_t handler_capacity;
static __thread size_t active_count;

int pthread_atfork(void (*prepare)(void), void (*parent)(void),
	void (*child)(void))
{
	struct atfork_handler *new_handlers;
	size_t capacity;
	RtlAcquirePebLock();
	if (handler_count == handler_capacity) {
		capacity = handler_capacity ? handler_capacity * 2 : 8;
		new_handlers = realloc(handlers, capacity * sizeof *handlers);
		if (!new_handlers) {
			RtlReleasePebLock();
			return ENOMEM;
		}
		handlers = new_handlers;
		handler_capacity = capacity;
	}
	handlers[handler_count].prepare = prepare;
	handlers[handler_count].parent = parent;
	handlers[handler_count].child = child;
	handler_count++;
	RtlReleasePebLock();
	return 0;
}

void __pthread_atfork_prepare(void)
{
	size_t i;
	RtlAcquirePebLock();
	active_count = handler_count;
	RtlReleasePebLock();
	for (i = active_count; i; i--)
		if (handlers[i - 1].prepare) handlers[i - 1].prepare();
}

void __pthread_atfork_parent(void)
{
	size_t i, count = active_count;
	active_count = 0;
	for (i = 0; i < count; i++)
		if (handlers[i].parent) handlers[i].parent();
}

void __pthread_atfork_child(void)
{
	size_t i, count = active_count;
	active_count = 0;
	for (i = 0; i < count; i++)
		if (handlers[i].child) handlers[i].child();
}
