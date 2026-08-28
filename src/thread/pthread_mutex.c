/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <pthread.h>
#include <errno.h>
#include <sched.h>
#include <string.h>
#include <time.h>
#include "pthread_impl.h"

#define MUTEX_MAGIC ((ULONG_PTR)0x4d555458u)
#define MUTEX_DEAD  ((ULONG_PTR)0x4d555444u)
#define MUTEXATTR_MAGIC ((ULONG_PTR)0x4d415454u)
#define ROBUST_CONSISTENT 0
#define ROBUST_OWNER_DEAD 1
#define ROBUST_NOT_RECOVERABLE 2
#define ROBUST_POLL_TICKS 100000LL

struct mutex_data {
	ULONG_PTR magic;
	HANDLE semaphore;
	pthread_t owner;
	unsigned recursion;
	unsigned waiters;
	unsigned char type;
	unsigned char pshared;
	unsigned char protocol;
	unsigned char prioceiling;
	unsigned char robust;
	unsigned char robust_state;
};

struct mutexattr_data {
	ULONG_PTR magic;
	unsigned char type;
	unsigned char pshared;
	unsigned char protocol;
	unsigned char prioceiling;
	unsigned char robust;
};

typedef char mutex_data_fits_public_storage[
	sizeof(struct mutex_data) <= sizeof(pthread_mutex_t) ? 1 : -1];
typedef char mutexattr_data_fits_public_storage[
	sizeof(struct mutexattr_data) <= sizeof(pthread_mutexattr_t) ? 1 : -1];

static struct mutex_data *mutex_data(pthread_mutex_t *mutex)
{
	return (struct mutex_data *)(void *)mutex;
}

static const struct mutex_data *const_mutex_data(const pthread_mutex_t *mutex)
{
	return (const struct mutex_data *)(const void *)mutex;
}

static struct mutexattr_data *mutexattr_data(pthread_mutexattr_t *attr)
{
	return (struct mutexattr_data *)(void *)attr;
}

static const struct mutexattr_data *const_mutexattr_data(
	const pthread_mutexattr_t *attr)
{
	return (const struct mutexattr_data *)(const void *)attr;
}

static int valid_type(int type)
{
	return type == PTHREAD_MUTEX_NORMAL || type == PTHREAD_MUTEX_ERRORCHECK ||
	       type == PTHREAD_MUTEX_RECURSIVE;
}

static int valid_pshared(int value)
{
	return value == PTHREAD_PROCESS_PRIVATE || value == PTHREAD_PROCESS_SHARED;
}

static int valid_protocol(int value)
{
	return value == PTHREAD_PRIO_NONE || value == PTHREAD_PRIO_INHERIT ||
	       value == PTHREAD_PRIO_PROTECT;
}

static int valid_robust(int value)
{
	return value == PTHREAD_MUTEX_STALLED || value == PTHREAD_MUTEX_ROBUST;
}

static int valid_ceiling(int value)
{
	return value >= sched_get_priority_min(SCHED_FIFO) &&
	       value <= sched_get_priority_max(SCHED_FIFO);
}

static int create_semaphore(HANDLE *output)
{
	NTSTATUS status = NtCreateSemaphore(output, SEMAPHORE_ALL_ACCESS, 0, 0,
		0x7fffffff);
	return NT_SUCCESS(status) ? 0 : EAGAIN;
}

static int mutex_ready(pthread_mutex_t *mutex)
{
	struct mutex_data *data;
	HANDLE semaphore;
	int error;
	if (!mutex) return EINVAL;
	data = mutex_data(mutex);
	RtlAcquirePebLock();
	if (data->magic == MUTEX_MAGIC) {
		RtlReleasePebLock();
		return 0;
	}
	if (data->magic == MUTEX_DEAD || data->magic != 0) {
		RtlReleasePebLock();
		return EINVAL;
	}
	RtlReleasePebLock();
	error = create_semaphore(&semaphore);
	if (error) return error;
	RtlAcquirePebLock();
	if (data->magic == 0) {
		memset(data, 0, sizeof *data);
		data->magic = MUTEX_MAGIC;
		data->semaphore = semaphore;
		data->type = PTHREAD_MUTEX_DEFAULT;
		data->pshared = PTHREAD_PROCESS_PRIVATE;
		data->protocol = PTHREAD_PRIO_NONE;
		data->prioceiling = sched_get_priority_min(SCHED_FIFO);
		data->robust = PTHREAD_MUTEX_STALLED;
		semaphore = 0;
	}
	error = data->magic == MUTEX_MAGIC ? 0 : EINVAL;
	RtlReleasePebLock();
	if (semaphore) NtClose(semaphore);
	return error;
}

int pthread_mutex_init(pthread_mutex_t *__restrict mutex,
	const pthread_mutexattr_t *__restrict attr)
{
	struct mutex_data *data;
	const struct mutexattr_data *attributes = 0;
	HANDLE semaphore;
	int error;
	if (!mutex) return EINVAL;
	if (attr) {
		attributes = const_mutexattr_data(attr);
		if (attributes->magic != MUTEXATTR_MAGIC) return EINVAL;
	}
	error = create_semaphore(&semaphore);
	if (error) return error;
	memset(mutex, 0, sizeof *mutex);
	data = mutex_data(mutex);
	data->magic = MUTEX_MAGIC;
	data->semaphore = semaphore;
	data->type = attributes ? attributes->type : PTHREAD_MUTEX_DEFAULT;
	data->pshared = attributes ? attributes->pshared : PTHREAD_PROCESS_PRIVATE;
	data->protocol = attributes ? attributes->protocol : PTHREAD_PRIO_NONE;
	data->prioceiling = attributes ? attributes->prioceiling :
		sched_get_priority_min(SCHED_FIFO);
	data->robust = attributes ? attributes->robust : PTHREAD_MUTEX_STALLED;
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	struct mutex_data *data;
	HANDLE semaphore;
	int error = mutex_ready(mutex);
	if (error) return error;
	data = mutex_data(mutex);
	RtlAcquirePebLock();
	if (data->owner || data->waiters) {
		RtlReleasePebLock();
		return EBUSY;
	}
	semaphore = data->semaphore;
	data->semaphore = 0;
	data->magic = MUTEX_DEAD;
	RtlReleasePebLock();
	if (semaphore) NtClose(semaphore);
	return 0;
}

static int mutex_acquire(pthread_mutex_t *mutex,
	const struct timespec *absolute, int try_only)
{
	struct mutex_data *data;
	struct __pthread *self;
	int error;
	__sig_drain_pending();
	error = mutex_ready(mutex);
	if (error) return error;
	if (absolute && (absolute->tv_sec < 0 || absolute->tv_nsec < 0 ||
	    absolute->tv_nsec >= 1000000000L)) return EINVAL;
	self = __pthread_current();
	if (!self) return EAGAIN;
	data = mutex_data(mutex);
	for (;;) {
		HANDLE semaphore;
		LARGE_INTEGER timeout = 0, *timeout_pointer = 0;
		NTSTATUS status;
		RtlAcquirePebLock();
		if (data->robust == PTHREAD_MUTEX_ROBUST && data->owner &&
		    data->owner->exited) {
			data->owner = self;
			data->recursion = 1;
			data->robust_state = ROBUST_OWNER_DEAD;
			RtlReleasePebLock();
			return EOWNERDEAD;
		}
		if (!data->owner) {
			if (data->robust == PTHREAD_MUTEX_ROBUST &&
			    data->robust_state == ROBUST_NOT_RECOVERABLE) {
				RtlReleasePebLock();
				return ENOTRECOVERABLE;
			}
			data->owner = self;
			data->recursion = 1;
			RtlReleasePebLock();
			return data->robust == PTHREAD_MUTEX_ROBUST &&
				data->robust_state == ROBUST_OWNER_DEAD ? EOWNERDEAD : 0;
		}
		if (data->owner == self) {
			if (data->type == PTHREAD_MUTEX_RECURSIVE) {
				data->recursion++;
				RtlReleasePebLock();
				return 0;
			}
			if (data->type == PTHREAD_MUTEX_ERRORCHECK || try_only) {
				RtlReleasePebLock();
				return try_only ? EBUSY : EDEADLK;
			}
		}
		if (try_only) {
			RtlReleasePebLock();
			return EBUSY;
		}
		if (absolute) {
			struct timespec now;
			long long ticks;
			clock_gettime(CLOCK_REALTIME, &now);
			ticks = ((long long)absolute->tv_sec - now.tv_sec) *
				10000000LL + (absolute->tv_nsec - now.tv_nsec + 99) / 100;
			if (ticks <= 0) {
				RtlReleasePebLock();
				return ETIMEDOUT;
			}
			timeout = -ticks;
			timeout_pointer = &timeout;
		}
		if (data->robust == PTHREAD_MUTEX_ROBUST &&
		    (!timeout_pointer || timeout < -ROBUST_POLL_TICKS)) {
			timeout = -ROBUST_POLL_TICKS;
			timeout_pointer = &timeout;
		}
		data->waiters++;
		semaphore = data->semaphore;
		RtlReleasePebLock();
		status = NtWaitForSingleObject(semaphore, TRUE, timeout_pointer);
		RtlAcquirePebLock();
		if (data->waiters) data->waiters--;
		RtlReleasePebLock();
		if (status == STATUS_TIMEOUT) {
			/* Recheck a robust owner's exit before deciding that an
			 * absolute deadline has expired. */
			if (data->robust == PTHREAD_MUTEX_ROBUST) continue;
			return ETIMEDOUT;
		}
		if (!NT_SUCCESS(status) && status != STATUS_USER_APC &&
		    status != STATUS_ALERTED) return EINVAL;
	}
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	return mutex_acquire(mutex, 0, 0);
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	return mutex_acquire(mutex, 0, 1);
}

int pthread_mutex_timedlock(pthread_mutex_t *__restrict mutex,
	const struct timespec *__restrict absolute)
{
	if (!absolute) return EINVAL;
	return mutex_acquire(mutex, absolute, 0);
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	struct mutex_data *data;
	struct __pthread *self = __pthread_current();
	int wake = 0;
	int error = mutex_ready(mutex);
	if (error) return error;
	if (!self) return EINVAL;
	data = mutex_data(mutex);
	RtlAcquirePebLock();
	if (data->owner != self && (data->type != PTHREAD_MUTEX_NORMAL ||
	    data->robust == PTHREAD_MUTEX_ROBUST)) {
		RtlReleasePebLock();
		return EPERM;
	}
	if (--data->recursion == 0) {
		if (data->robust == PTHREAD_MUTEX_ROBUST &&
		    data->robust_state == ROBUST_OWNER_DEAD)
			data->robust_state = ROBUST_NOT_RECOVERABLE;
		data->owner = 0;
		wake = data->waiters != 0;
	}
	RtlReleasePebLock();
	if (wake) NtReleaseSemaphore(data->semaphore, 1, 0);
	return 0;
}

int pthread_mutex_getprioceiling(const pthread_mutex_t *__restrict mutex,
	int *__restrict ceiling)
{
	const struct mutex_data *data;
	if (!mutex || !ceiling || const_mutex_data(mutex)->magic != MUTEX_MAGIC)
		return EINVAL;
	data = const_mutex_data(mutex);
	RtlAcquirePebLock();
	*ceiling = data->prioceiling;
	RtlReleasePebLock();
	return 0;
}

int pthread_mutex_setprioceiling(pthread_mutex_t *__restrict mutex,
	int ceiling, int *__restrict old_ceiling)
{
	struct mutex_data *data;
	int error;
	if (!old_ceiling || !valid_ceiling(ceiling)) return EINVAL;
	error = pthread_mutex_lock(mutex);
	if (error) return error;
	data = mutex_data(mutex);
	*old_ceiling = data->prioceiling;
	data->prioceiling = ceiling;
	return pthread_mutex_unlock(mutex);
}

int pthread_mutex_consistent(pthread_mutex_t *mutex)
{
	struct mutex_data *data;
	struct __pthread *self;
	int error = EINVAL;
	if (!mutex || mutex_data(mutex)->magic != MUTEX_MAGIC) return EINVAL;
	self = __pthread_current();
	if (!self) return EINVAL;
	data = mutex_data(mutex);
	RtlAcquirePebLock();
	if (data->robust == PTHREAD_MUTEX_ROBUST && data->owner == self &&
	    data->robust_state == ROBUST_OWNER_DEAD) {
		data->robust_state = ROBUST_CONSISTENT;
		error = 0;
	}
	RtlReleasePebLock();
	return error;
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
	struct mutexattr_data *data;
	if (!attr) return EINVAL;
	memset(attr, 0, sizeof *attr);
	data = mutexattr_data(attr);
	data->magic = MUTEXATTR_MAGIC;
	data->type = PTHREAD_MUTEX_DEFAULT;
	data->pshared = PTHREAD_PROCESS_PRIVATE;
	data->protocol = PTHREAD_PRIO_NONE;
	data->prioceiling = sched_get_priority_min(SCHED_FIFO);
	data->robust = PTHREAD_MUTEX_STALLED;
	return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
	if (!attr || mutexattr_data(attr)->magic != MUTEXATTR_MAGIC) return EINVAL;
	memset(attr, 0, sizeof *attr);
	return 0;
}

int pthread_mutexattr_getpshared(const pthread_mutexattr_t *__restrict attr,
	int *__restrict output)
{
	if (!attr || !output || const_mutexattr_data(attr)->magic != MUTEXATTR_MAGIC)
		return EINVAL;
	*output = const_mutexattr_data(attr)->pshared;
	return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *__restrict attr,
	int *__restrict output)
{
	if (!attr || !output || const_mutexattr_data(attr)->magic != MUTEXATTR_MAGIC)
		return EINVAL;
	*output = const_mutexattr_data(attr)->type;
	return 0;
}

int pthread_mutexattr_getprotocol(const pthread_mutexattr_t *__restrict attr,
	int *__restrict output)
{
	if (!attr || !output || const_mutexattr_data(attr)->magic != MUTEXATTR_MAGIC)
		return EINVAL;
	*output = const_mutexattr_data(attr)->protocol;
	return 0;
}

int pthread_mutexattr_getprioceiling(const pthread_mutexattr_t *__restrict attr,
	int *__restrict output)
{
	if (!attr || !output || const_mutexattr_data(attr)->magic != MUTEXATTR_MAGIC)
		return EINVAL;
	*output = const_mutexattr_data(attr)->prioceiling;
	return 0;
}

int pthread_mutexattr_getrobust(const pthread_mutexattr_t *__restrict attr,
	int *__restrict output)
{
	if (!attr || !output || const_mutexattr_data(attr)->magic != MUTEXATTR_MAGIC)
		return EINVAL;
	*output = const_mutexattr_data(attr)->robust;
	return 0;
}

int pthread_mutexattr_setpshared(pthread_mutexattr_t *attr, int value)
{
	if (!attr || mutexattr_data(attr)->magic != MUTEXATTR_MAGIC ||
	    !valid_pshared(value)) return EINVAL;
	mutexattr_data(attr)->pshared = value;
	return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int value)
{
	if (!attr || mutexattr_data(attr)->magic != MUTEXATTR_MAGIC ||
	    !valid_type(value)) return EINVAL;
	mutexattr_data(attr)->type = value;
	return 0;
}

int pthread_mutexattr_setprotocol(pthread_mutexattr_t *attr, int value)
{
	if (!attr || mutexattr_data(attr)->magic != MUTEXATTR_MAGIC ||
	    !valid_protocol(value)) return EINVAL;
	mutexattr_data(attr)->protocol = value;
	return 0;
}

int pthread_mutexattr_setprioceiling(pthread_mutexattr_t *attr, int value)
{
	if (!attr || mutexattr_data(attr)->magic != MUTEXATTR_MAGIC ||
	    !valid_ceiling(value)) return EINVAL;
	mutexattr_data(attr)->prioceiling = value;
	return 0;
}

int pthread_mutexattr_setrobust(pthread_mutexattr_t *attr, int value)
{
	if (!attr || mutexattr_data(attr)->magic != MUTEXATTR_MAGIC ||
	    !valid_robust(value)) return EINVAL;
	mutexattr_data(attr)->robust = value;
	return 0;
}
