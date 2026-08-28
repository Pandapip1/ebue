/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pthread_impl.h"

#define RWLOCK_MAGIC ((ULONG_PTR)0x52574c4bu)
#define RWLOCK_DEAD ((ULONG_PTR)0x52574c58u)
#define RWATTR_MAGIC ((ULONG_PTR)0x52574154u)

struct rw_waiter;

struct rwlock_data {
	ULONG_PTR magic;
	struct rw_waiter *head;
	struct rw_waiter *tail;
	pthread_t writer;
	unsigned readers;
	unsigned waiting_writers;
	int pshared;
	volatile int shared_state;
};

struct rw_waiter {
	struct rw_waiter *previous;
	struct rw_waiter *next;
	struct rwlock_data *lock;
	HANDLE semaphore;
	pthread_t owner;
	int write;
	int linked;
};

struct rwattr_data {
	ULONG_PTR magic;
	int pshared;
};

static struct rwlock_data *rwlock_data(pthread_rwlock_t *lock)
{
	return (struct rwlock_data *)(void *)lock;
}

static const struct rwattr_data *const_rwattr_data(
	const pthread_rwlockattr_t *attr)
{
	return (const struct rwattr_data *)(const void *)attr;
}

static struct rwattr_data *rwattr_data(pthread_rwlockattr_t *attr)
{
	return (struct rwattr_data *)(void *)attr;
}

static int rwlock_ready(pthread_rwlock_t *lock)
{
	struct rwlock_data *data;
	if (!lock) return EINVAL;
	data = rwlock_data(lock);
	RtlAcquirePebLock();
	if (data->magic == RWLOCK_MAGIC) {
		RtlReleasePebLock();
		return 0;
	}
	if (data->magic == RWLOCK_DEAD || data->magic != 0) {
		RtlReleasePebLock();
		return EINVAL;
	}
	memset(data, 0, sizeof *data);
	data->magic = RWLOCK_MAGIC;
	data->pshared = PTHREAD_PROCESS_PRIVATE;
	RtlReleasePebLock();
	return 0;
}

static int compare_exchange(volatile int *address, int old_value,
	int new_value)
{
	int previous;
	__asm__ __volatile__("lock; cmpxchgl %2, %1"
		: "=a"(previous), "+m"(*address)
		: "r"(new_value), "0"(old_value) : "memory");
	return previous;
}

static int shared_acquire(struct rwlock_data *data,
	const struct timespec *absolute, int try_only, int write)
{
	for (;;) {
		int state = data->shared_state;
		if (write ? state == 0 : state >= 0) {
			int next = write ? -1 : state + 1;
			if (compare_exchange(&data->shared_state, state, next) == state)
				return 0;
			continue;
		}
		if (try_only) return EBUSY;
		if (!absolute || absolute->tv_nsec < 0 ||
		    absolute->tv_nsec >= 1000000000L) return EINVAL;
		{
			struct timespec now;
			clock_gettime(CLOCK_REALTIME, &now);
			if (now.tv_sec > absolute->tv_sec ||
			    (now.tv_sec == absolute->tv_sec &&
			     now.tv_nsec >= absolute->tv_nsec)) return ETIMEDOUT;
		}
		sched_yield();
	}
}

static void unlink_waiter(struct rw_waiter *waiter)
{
	struct rwlock_data *data = waiter->lock;
	if (!waiter->linked) return;
	if (waiter->previous) waiter->previous->next = waiter->next;
	else data->head = waiter->next;
	if (waiter->next) waiter->next->previous = waiter->previous;
	else data->tail = waiter->previous;
	if (waiter->write) data->waiting_writers--;
	waiter->linked = 0;
}

static void wake_waiters(struct rwlock_data *data)
{
	struct rw_waiter *waiter, *next;
	if (data->writer) return;
	if (!data->readers && data->waiting_writers) {
		for (waiter = data->head; waiter; waiter = waiter->next) {
			if (!waiter->write) continue;
			data->writer = waiter->owner;
			unlink_waiter(waiter);
			NtReleaseSemaphore(waiter->semaphore, 1, 0);
			return;
		}
	}
	if (data->waiting_writers) return;
	for (waiter = data->head; waiter; waiter = next) {
		next = waiter->next;
		if (waiter->write) continue;
		data->readers++;
		unlink_waiter(waiter);
		NtReleaseSemaphore(waiter->semaphore, 1, 0);
	}
}

static void wait_cleanup(void *argument)
{
	struct rw_waiter *waiter = argument;
	RtlAcquirePebLock();
	if (waiter->linked) {
		unlink_waiter(waiter);
		wake_waiters(waiter->lock);
	}
	RtlReleasePebLock();
	if (waiter->semaphore) {
		NtClose(waiter->semaphore);
		waiter->semaphore = 0;
	}
	free(waiter);
}

static int rwlock_acquire(pthread_rwlock_t *lock,
	const struct timespec *absolute, int try_only, int write)
{
	struct rwlock_data *data;
	struct rw_waiter *waiter;
	pthread_t self;
	int result = 0, old_state = PTHREAD_CANCEL_ENABLE;
	int error = rwlock_ready(lock);
	if (error) return error;
	self = pthread_self();
	if (!self) return EAGAIN;
	data = rwlock_data(lock);
	if (data->pshared == PTHREAD_PROCESS_SHARED)
		return shared_acquire(data, absolute, try_only, write);
	RtlAcquirePebLock();
	if (write ? (!data->writer && !data->readers) :
	    (!data->writer && !data->waiting_writers)) {
		if (write) data->writer = self;
		else data->readers++;
		RtlReleasePebLock();
		return 0;
	}
	if (data->writer == self) {
		RtlReleasePebLock();
		return try_only ? EBUSY : EDEADLK;
	}
	if (try_only) {
		RtlReleasePebLock();
		return EBUSY;
	}
	if (!absolute || absolute->tv_sec < 0 || absolute->tv_nsec < 0 ||
	    absolute->tv_nsec >= 1000000000L) {
		RtlReleasePebLock();
		return EINVAL;
	}
	RtlReleasePebLock();
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state);
	waiter = calloc(1, sizeof *waiter);
	if (!waiter) {
		pthread_setcancelstate(old_state, 0);
		return EAGAIN;
	}
	waiter->lock = data;
	waiter->write = write;
	waiter->owner = self;
	if (!NT_SUCCESS(NtCreateSemaphore(&waiter->semaphore, SEMAPHORE_ALL_ACCESS,
		0, 0, 1))) {
		free(waiter);
		pthread_setcancelstate(old_state, 0);
		return EAGAIN;
	}
	pthread_cleanup_push(wait_cleanup, waiter);
	RtlAcquirePebLock();
	if (write ? (!data->writer && !data->readers) :
	    (!data->writer && !data->waiting_writers)) {
		if (write) data->writer = self;
		else data->readers++;
		RtlReleasePebLock();
		pthread_setcancelstate(old_state, 0);
		goto done;
	}
	if (data->writer == self) {
		result = EDEADLK;
		RtlReleasePebLock();
		pthread_setcancelstate(old_state, 0);
		goto done;
	}
	waiter->previous = data->tail;
	if (data->tail) data->tail->next = waiter;
	else data->head = waiter;
	data->tail = waiter;
	waiter->linked = 1;
	if (write) data->waiting_writers++;
	RtlReleasePebLock();
	pthread_setcancelstate(old_state, 0);
	for (;;) {
		struct timespec now;
		LARGE_INTEGER timeout;
		long long ticks;
		NTSTATUS status;
		clock_gettime(CLOCK_REALTIME, &now);
		ticks = __timespec_diff_ticks(absolute->tv_sec,
			absolute->tv_nsec, now.tv_sec, now.tv_nsec);
		if (ticks <= 0) status = STATUS_TIMEOUT;
		else {
			if (ticks <= INT64_MAX - 10000) ticks += 10000;
			timeout = -ticks;
			status = NtWaitForSingleObject(waiter->semaphore, TRUE, &timeout);
		}
		if (status == STATUS_USER_APC || status == STATUS_ALERTED) continue;
		if (status == STATUS_TIMEOUT) {
			RtlAcquirePebLock();
			if (waiter->linked) {
				unlink_waiter(waiter);
				wake_waiters(data);
				result = ETIMEDOUT;
			}
			RtlReleasePebLock();
			break;
		}
		if (!NT_SUCCESS(status)) result = EINVAL;
		break;
	}
	done:
	pthread_cleanup_pop(1);
	return result;
}

int pthread_rwlock_init(pthread_rwlock_t *__restrict lock,
	const pthread_rwlockattr_t *__restrict attr)
{
	struct rwlock_data *data;
	const struct rwattr_data *attributes = 0;
	if (!lock) return EINVAL;
	if (attr) {
		attributes = const_rwattr_data(attr);
		if (attributes->magic != RWATTR_MAGIC) return EINVAL;
	}
	memset(lock, 0, sizeof *lock);
	data = rwlock_data(lock);
	data->magic = RWLOCK_MAGIC;
	data->pshared = attributes ? attributes->pshared : PTHREAD_PROCESS_PRIVATE;
	return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *lock)
{
	struct rwlock_data *data;
	int error = rwlock_ready(lock);
	if (error) return error;
	data = rwlock_data(lock);
	if (data->pshared == PTHREAD_PROCESS_SHARED) {
		if (data->shared_state) return EBUSY;
		data->magic = RWLOCK_DEAD;
		return 0;
	}
	RtlAcquirePebLock();
	if (data->writer || data->readers || data->head) error = EBUSY;
	else data->magic = RWLOCK_DEAD;
	RtlReleasePebLock();
	return error;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *lock)
{
	struct timespec forever = {(time_t)0x7fffffff, 0};
	return rwlock_acquire(lock, &forever, 0, 0);
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *lock)
{
	return rwlock_acquire(lock, 0, 1, 0);
}

int pthread_rwlock_timedrdlock(pthread_rwlock_t *__restrict lock,
	const struct timespec *__restrict absolute)
{
	return rwlock_acquire(lock, absolute, 0, 0);
}

int pthread_rwlock_wrlock(pthread_rwlock_t *lock)
{
	struct timespec forever = {(time_t)0x7fffffff, 0};
	return rwlock_acquire(lock, &forever, 0, 1);
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *lock)
{
	return rwlock_acquire(lock, 0, 1, 1);
}

int pthread_rwlock_timedwrlock(pthread_rwlock_t *__restrict lock,
	const struct timespec *__restrict absolute)
{
	return rwlock_acquire(lock, absolute, 0, 1);
}

int pthread_rwlock_unlock(pthread_rwlock_t *lock)
{
	struct rwlock_data *data;
	pthread_t self = pthread_self();
	int error = rwlock_ready(lock);
	if (error) return error;
	data = rwlock_data(lock);
	if (data->pshared == PTHREAD_PROCESS_SHARED) {
		for (;;) {
			int state = data->shared_state;
			if (!state) return EINVAL;
			if (compare_exchange(&data->shared_state, state,
				state < 0 ? 0 : state - 1) == state) return 0;
		}
	}
	RtlAcquirePebLock();
	if (data->writer == self) data->writer = 0;
	else if (data->readers) data->readers--;
	else error = EINVAL;
	if (!error) wake_waiters(data);
	RtlReleasePebLock();
	return error;
}

int pthread_rwlockattr_init(pthread_rwlockattr_t *attr)
{
	struct rwattr_data *data;
	if (!attr) return EINVAL;
	memset(attr, 0, sizeof *attr);
	data = rwattr_data(attr);
	data->magic = RWATTR_MAGIC;
	data->pshared = PTHREAD_PROCESS_PRIVATE;
	return 0;
}

int pthread_rwlockattr_destroy(pthread_rwlockattr_t *attr)
{
	if (!attr || rwattr_data(attr)->magic != RWATTR_MAGIC) return EINVAL;
	memset(attr, 0, sizeof *attr);
	return 0;
}

int pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *__restrict attr,
	int *__restrict pshared)
{
	if (!attr || !pshared || const_rwattr_data(attr)->magic != RWATTR_MAGIC)
		return EINVAL;
	*pshared = const_rwattr_data(attr)->pshared;
	return 0;
}

int pthread_rwlockattr_setpshared(pthread_rwlockattr_t *attr, int pshared)
{
	if (!attr || rwattr_data(attr)->magic != RWATTR_MAGIC ||
	    (pshared != PTHREAD_PROCESS_PRIVATE &&
	     pshared != PTHREAD_PROCESS_SHARED)) return EINVAL;
	rwattr_data(attr)->pshared = pshared;
	return 0;
}
