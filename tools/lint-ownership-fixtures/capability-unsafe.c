/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "../../include/ownership.h"

tokdef mutex_unlocked implicit_drop;
tokdef mutex_locked l_unlimited implicit_drop;
tokdef rwlock_unlocked implicit_drop;
tokdef rwlock_shared l_unlimited implicit_drop;
tokdef rwlock_exclusive implicit_drop;
tokdef allocation implicit_drop;

typedef struct { void *opaque[8]; } mutex_t;
typedef struct { void *opaque[12]; } rwlock_t;

int mutex_init(mutex_t *mutex
    construct(mutex) grant(mutex_unlocked));
int mutex_lock(mutex_t *mutex
    handle(mutex) consume(mutex_unlocked) grant(mutex_locked));
int mutex_unlock(mutex_t *mutex
    handle(mutex) consume(mutex_locked) grant(mutex_unlocked));
int mutex_destroy(mutex_t *mutex
    destroy(mutex) consume(mutex_unlocked));

int grant_linear(mutex_t *object grant(allocation));

void lock_without_unlocked_token(mutex_t *mutex)
{
	mutex_lock(mutex); /* ownership-expect: capability-missing */
}

void lock_twice(void)
{
	mutex_t mutex;
	if (mutex_init(&mutex) != 0)
		return;
	if (mutex_lock(&mutex) == 0)
		mutex_lock(&mutex); /* ownership-expect: capability-lock-twice */
}

void unlock_twice(void)
{
	mutex_t mutex;
	if (mutex_init(&mutex) != 0)
		return;
	if (mutex_lock(&mutex) != 0)
		return;
	if (mutex_unlock(&mutex) == 0)
		mutex_unlock(&mutex); /* ownership-expect: capability-unlock-twice */
}

void destroy_while_locked(void)
{
	mutex_t mutex;
	if (mutex_init(&mutex) != 0)
		return;
	if (mutex_lock(&mutex) == 0)
		mutex_destroy(&mutex); /* ownership-expect: capability-destroy-locked */
}

void duplicate_linear_token(void)
{
	mutex_t object;
	if (grant_linear(&object) == 0)
		grant_linear(&object); /* ownership-expect: capability-linear-duplicate */
}
