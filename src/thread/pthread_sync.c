/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <pthread.h>
#include <errno.h>
#include <sched.h>
#include <string.h>
#include "pthread_impl.h"

#define SPIN_UNLOCKED 1
#define SPIN_LOCKED 2
#define BARRIER_MAGIC ((ULONG_PTR)0x42415252u)
#define BARRIER_DEAD ((ULONG_PTR)0x42415258u)
#define BARATTR_MAGIC ((ULONG_PTR)0x42415454u)

struct barrier_data {
	ULONG_PTR magic;
	volatile int guard;
	volatile unsigned generation;
	unsigned count;
	unsigned waiting;
	int pshared;
};

struct barrierattr_data {
	ULONG_PTR magic;
	int pshared;
};

static int compare_exchange(volatile int *address, int old_value,
	int new_value)
{
	int previous;
	__asm__ __volatile__("lock; cmpxchgl %2, %1"
		: "=a"(previous), "+m"(*address)
		: "r"(new_value), "0"(old_value) : "memory");
	return previous;
}

static void acquire_guard(volatile int *guard)
{
	while (compare_exchange(guard, 0, 1) != 0) sched_yield();
}

static void release_guard(volatile int *guard)
{
	__asm__ __volatile__("" : : : "memory");
	*guard = 0;
}

static void alertable_yield(void)
{
	LARGE_INTEGER delay = 0;
	NtDelayExecution(TRUE, &delay);
	sched_yield();
}

int pthread_spin_init(pthread_spinlock_t *lock, int pshared)
{
	if (!lock || (pshared != PTHREAD_PROCESS_PRIVATE &&
	    pshared != PTHREAD_PROCESS_SHARED)) return EINVAL;
	lock->__value = SPIN_UNLOCKED;
	return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *lock)
{
	if (!lock || lock->__value != SPIN_UNLOCKED) return EBUSY;
	lock->__value = 0;
	return 0;
}

int pthread_spin_lock(pthread_spinlock_t *lock)
{
	if (!lock) return EINVAL;
	for (;;) {
		int state = lock->__value;
		if (!state) return EINVAL;
		if (state == SPIN_UNLOCKED &&
		    compare_exchange(&lock->__value, SPIN_UNLOCKED,
			SPIN_LOCKED) == SPIN_UNLOCKED) return 0;
		alertable_yield();
	}
}

int pthread_spin_trylock(pthread_spinlock_t *lock)
{
	int state;
	if (!lock) return EINVAL;
	state = lock->__value;
	if (!state) return EINVAL;
	return state == SPIN_UNLOCKED &&
		compare_exchange(&lock->__value, SPIN_UNLOCKED,
			SPIN_LOCKED) == SPIN_UNLOCKED ? 0 : EBUSY;
}

int pthread_spin_unlock(pthread_spinlock_t *lock)
{
	if (!lock || lock->__value != SPIN_LOCKED) return EINVAL;
	__asm__ __volatile__("" : : : "memory");
	lock->__value = SPIN_UNLOCKED;
	return 0;
}

static struct barrier_data *barrier_data(pthread_barrier_t *barrier)
{
	return (struct barrier_data *)(void *)barrier;
}

static struct barrierattr_data *barrierattr_data(pthread_barrierattr_t *attr)
{
	return (struct barrierattr_data *)(void *)attr;
}

static const struct barrierattr_data *const_barrierattr_data(
	const pthread_barrierattr_t *attr)
{
	return (const struct barrierattr_data *)(const void *)attr;
}

int pthread_barrier_init(pthread_barrier_t *__restrict barrier,
	const pthread_barrierattr_t *__restrict attr, unsigned count)
{
	struct barrier_data *data;
	const struct barrierattr_data *attributes = 0;
	if (!barrier || !count) return EINVAL;
	if (attr) {
		attributes = const_barrierattr_data(attr);
		if (attributes->magic != BARATTR_MAGIC) return EINVAL;
	}
	memset(barrier, 0, sizeof *barrier);
	data = barrier_data(barrier);
	data->magic = BARRIER_MAGIC;
	data->count = count;
	data->pshared = attributes ? attributes->pshared :
		PTHREAD_PROCESS_PRIVATE;
	return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
	struct barrier_data *data;
	int result = 0;
	if (!barrier) return EINVAL;
	data = barrier_data(barrier);
	if (data->magic != BARRIER_MAGIC) return EINVAL;
	acquire_guard(&data->guard);
	if (data->waiting) result = EBUSY;
	else data->magic = BARRIER_DEAD;
	release_guard(&data->guard);
	return result;
}

int pthread_barrier_wait(pthread_barrier_t *barrier)
{
	struct barrier_data *data;
	unsigned generation;
	if (!barrier) return EINVAL;
	data = barrier_data(barrier);
	if (data->magic != BARRIER_MAGIC) return EINVAL;
	acquire_guard(&data->guard);
	generation = data->generation;
	if (++data->waiting == data->count) {
		data->waiting = 0;
		data->generation++;
		release_guard(&data->guard);
		return PTHREAD_BARRIER_SERIAL_THREAD;
	}
	release_guard(&data->guard);
	while (data->generation == generation) alertable_yield();
	return 0;
}

int pthread_barrierattr_init(pthread_barrierattr_t *attr)
{
	struct barrierattr_data *data;
	if (!attr) return EINVAL;
	memset(attr, 0, sizeof *attr);
	data = barrierattr_data(attr);
	data->magic = BARATTR_MAGIC;
	data->pshared = PTHREAD_PROCESS_PRIVATE;
	return 0;
}

int pthread_barrierattr_destroy(pthread_barrierattr_t *attr)
{
	if (!attr || barrierattr_data(attr)->magic != BARATTR_MAGIC) return EINVAL;
	memset(attr, 0, sizeof *attr);
	return 0;
}

int pthread_barrierattr_getpshared(const pthread_barrierattr_t *__restrict attr,
	int *__restrict pshared)
{
	if (!attr || !pshared ||
	    const_barrierattr_data(attr)->magic != BARATTR_MAGIC) return EINVAL;
	*pshared = const_barrierattr_data(attr)->pshared;
	return 0;
}

int pthread_barrierattr_setpshared(pthread_barrierattr_t *attr, int pshared)
{
	if (!attr || barrierattr_data(attr)->magic != BARATTR_MAGIC ||
	    (pshared != PTHREAD_PROCESS_PRIVATE &&
	     pshared != PTHREAD_PROCESS_SHARED)) return EINVAL;
	barrierattr_data(attr)->pshared = pshared;
	return 0;
}
