/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <pthread.h>
#include <errno.h>
#include <sched.h>
#include <stdlib.h>
#include "pthread_impl.h"

struct key_slot {
	unsigned generation;
	int allocated;
	void (*destructor)(void *);
};

static struct key_slot keys[PTHREAD_KEYS_MAX];

static unsigned key_index(pthread_key_t key)
{
	return key % PTHREAD_KEYS_MAX;
}

static pthread_key_t key_value(unsigned index, unsigned generation)
{
	return generation * PTHREAD_KEYS_MAX + index;
}

static int valid_key(pthread_key_t key)
{
	unsigned index = key_index(key);
	return keys[index].allocated &&
	       key_value(index, keys[index].generation) == key;
}

int pthread_key_create(pthread_key_t *output, void (*destructor)(void *))
{
	unsigned index;
	if (!output) return EINVAL;
	RtlAcquirePebLock();
	for (index = 0; index < PTHREAD_KEYS_MAX; index++) {
		if (keys[index].allocated) continue;
		keys[index].allocated = 1;
		keys[index].destructor = destructor;
		*output = key_value(index, keys[index].generation);
		RtlReleasePebLock();
		return 0;
	}
	RtlReleasePebLock();
	return EAGAIN;
}

int pthread_key_delete(pthread_key_t key)
{
	unsigned index = key_index(key);
	RtlAcquirePebLock();
	if (!valid_key(key)) {
		RtlReleasePebLock();
		return EINVAL;
	}
	keys[index].allocated = 0;
	keys[index].destructor = 0;
	keys[index].generation++;
	RtlReleasePebLock();
	return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
	struct __pthread *self;
	unsigned index = key_index(key);
	RtlAcquirePebLock();
	if (!valid_key(key)) {
		RtlReleasePebLock();
		return 0;
	}
	RtlReleasePebLock();
	self = __pthread_current();
	if (!self || !self->specific || self->specific[index].key != key)
		return 0;
	return self->specific[index].value;
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
	struct __pthread *self;
	unsigned index = key_index(key);
	RtlAcquirePebLock();
	if (!valid_key(key)) {
		RtlReleasePebLock();
		return EINVAL;
	}
	RtlReleasePebLock();
	self = __pthread_current();
	if (!self) return ENOMEM;
	if (!self->specific) {
		if (!value) return 0;
		self->specific = calloc(PTHREAD_KEYS_MAX, sizeof *self->specific);
		if (!self->specific) return ENOMEM;
	}
	self->specific[index].key = key;
	self->specific[index].value = (void *)value;
	return 0;
}

void __pthread_run_specific_destructors(struct __pthread *self)
{
	unsigned iteration, index;
	if (!self->specific) return;
	for (iteration = 0; iteration < PTHREAD_DESTRUCTOR_ITERATIONS; iteration++) {
		int called = 0;
		for (index = 0; index < PTHREAD_KEYS_MAX; index++) {
			struct __pthread_specific *specific = &self->specific[index];
			void (*destructor)(void *) = 0;
			void *value = specific->value;
			if (!value) continue;
			RtlAcquirePebLock();
			if (valid_key(specific->key))
				destructor = keys[index].destructor;
			specific->value = 0;
			RtlReleasePebLock();
			if (destructor) {
				called = 1;
				destructor(value);
			}
		}
		if (!called) break;
	}
	free(self->specific);
	self->specific = 0;
}

struct once_cleanup {
	pthread_once_t *control;
};

static void reset_once(void *argument)
{
	struct once_cleanup *cleanup = argument;
	RtlAcquirePebLock();
	*cleanup->control = PTHREAD_ONCE_INIT;
	RtlReleasePebLock();
}

int pthread_once(pthread_once_t *control, void (*initialize)(void))
{
	for (;;) {
		RtlAcquirePebLock();
		if (*control == 2) {
			RtlReleasePebLock();
			return 0;
		}
		if (*control == PTHREAD_ONCE_INIT) {
			struct once_cleanup reset = { control };
			*control = 1;
			RtlReleasePebLock();
			pthread_cleanup_push(reset_once, &reset);
			initialize();
			pthread_cleanup_pop(0);
			RtlAcquirePebLock();
			*control = 2;
			RtlReleasePebLock();
			return 0;
		}
		RtlReleasePebLock();
		sched_yield();
	}
}
