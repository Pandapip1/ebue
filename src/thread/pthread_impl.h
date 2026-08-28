/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PTHREAD_IMPL_H
#define PTHREAD_IMPL_H

#include <pthread.h>
#include <signal.h>
#include "libc.h"

#define PTHREAD_MAGIC 0x50544852u
#define PTHREAD_ATTR_MAGIC ((ULONG_PTR)0x41545452u)

struct __pthread_attr_data {
	ULONG_PTR magic;
	size_t stack_size;
	void *stack_address;
	size_t guard_size;
	int detach_state;
	int scope;
	int inherit_sched;
	int sched_policy;
	int sched_priority;
};

struct __pthread_specific {
	pthread_key_t key;
	void *value;
};

struct __pthread {
	unsigned magic;
	HANDLE handle;
	void *result;
	void *(*start)(void *);
	void *argument;
	int detached;
	int exited;
	int joining;
	int joined;
	int cancel_state;
	int cancel_type;
	int cancel_pending;
	int cancel_queued;
	volatile int cancel_running;
	volatile int cancel_unsafe_depth;
	const char *cancel_unsafe_region;
	volatile int cancel_defer_depth;
	int sched_policy;
	int sched_priority;
	sigset_t sigmask;
	struct __pthread_cleanup *cleanup;
	struct __pthread_specific *specific;
};

extern __thread struct __pthread *__pthread_self_control;
struct __pthread *__pthread_current(void);
void __pthread_adopt_current(struct __pthread *);
int __pthread_is_current(struct __pthread *);
void __pthread_run_specific_destructors(struct __pthread *);
void __pthread_cancel_current(void);
_Noreturn void __pthread_cancel_redirected(void);
void __pthread_testcancel(void);
void __pthread_cancel_unsafe_enter(const char *);
void __pthread_cancel_unsafe_leave(void);
int __pthread_cancel_unsafe_active(pthread_t);
void __pthread_cancel_defer_enter(void);
void __pthread_cancel_defer_leave(void);

#endif
