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
int duplicate_unlock_authority(mutex_t *mutex
    handle(mutex) withtok(mutex_locked) grant(mutex_locked));
int inspect_while_locked(mutex_t *mutex
    handle(mutex) withtok(mutex_locked));
int mutex_destroy(mutex_t *mutex
    destroy(mutex) consume(mutex_unlocked));

int rwlock_init(rwlock_t *lock
    construct(rwlock) grant(rwlock_unlocked));
int rwlock_read_lock(rwlock_t *lock
    handle(rwlock) consume(rwlock_unlocked) grant(rwlock_shared));
int rwlock_write_lock(rwlock_t *lock
    handle(rwlock) consume(rwlock_unlocked) grant(rwlock_exclusive));
int rwlock_unlock(rwlock_t *lock
    handle(rwlock) consume_any(rwlock_shared) consume_any(rwlock_exclusive) grant(rwlock_unlocked));
int rwlock_destroy(rwlock_t *lock
    destroy(rwlock) consume(rwlock_unlocked));

void delegated_mutex_unlock(void)
{
	mutex_t mutex;
	mutex_t *alias = &mutex;
	if (mutex_init(&mutex) != 0)
		return;
	if (mutex_lock(&mutex) == 0) {
		duplicate_unlock_authority(alias);
		inspect_while_locked(&mutex);
		if (mutex_unlock(alias) != 0)
			return;
	}
	mutex_destroy(&mutex);
}

void shared_and_exclusive_rwlock_epochs(int write)
{
	rwlock_t lock;
	if (rwlock_init(&lock) != 0)
		return;
	if (write) {
		if (rwlock_write_lock(&lock) == 0 && rwlock_unlock(&lock) != 0)
			return;
	} else if (rwlock_read_lock(&lock) == 0) {
		if (rwlock_unlock(&lock) != 0)
			return;
	}
	rwlock_destroy(&lock);
}
