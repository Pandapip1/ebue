/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef struct { void *opaque[8]; } mutex_t;
typedef struct { void *opaque[12]; } rwlock_t;

int mutex_init(mutex_t *mutex
    [[ownership_constructs(mutex), ownership_adds_token(mutex_unlocked)]]);
int mutex_lock(mutex_t *mutex
    [[ownership_requires_handle(mutex),
      ownership_drops_token(mutex_unlocked),
      ownership_adds_duplicable_token(mutex_locked)]]);
int mutex_unlock(mutex_t *mutex
    [[ownership_requires_handle(mutex), ownership_drops_token(mutex_locked),
      ownership_adds_token(mutex_unlocked)]]);
int mutex_destroy(mutex_t *mutex
    [[ownership_destroys(mutex), ownership_drops_token(mutex_unlocked)]]);

int grant_linear(mutex_t *object [[ownership_adds_token(allocation)]]);

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
