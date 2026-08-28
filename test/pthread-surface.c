/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>

static int fails;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fails++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
	} \
} while (0)

static void test_atfork(void)
{
	int i;
	/* Cross the initial eight-handler allocation so the checked
	 * collection-growth path is part of the public integration test. */
	for (i = 0; i < 10; i++) CHECK(pthread_atfork(0, 0, 0) == 0);
}

static void test_barrier_attributes(void)
{
	pthread_barrierattr_t attr;
	int pshared = -1;

	CHECK(pthread_barrierattr_init(&attr) == 0);
	CHECK(pthread_barrierattr_getpshared(&attr, &pshared) == 0);
	CHECK(pshared == PTHREAD_PROCESS_PRIVATE);
	CHECK(pthread_barrierattr_setpshared(&attr, PTHREAD_PROCESS_PRIVATE) == 0);
	CHECK(pthread_barrierattr_destroy(&attr) == 0);
}

static void test_mutex_extensions(void)
{
	pthread_mutexattr_t attr;
	pthread_mutex_t mutex;
	struct timespec now;
	int ceiling = -1, old_ceiling = -1;
	int protocol = -1, pshared = -1, robust = -1;

	CHECK(pthread_mutexattr_init(&attr) == 0);
	CHECK(pthread_mutexattr_getpshared(&attr, &pshared) == 0);
	CHECK(pshared == PTHREAD_PROCESS_PRIVATE);
	CHECK(pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_PRIVATE) == 0);
	CHECK(pthread_mutexattr_getprotocol(&attr, &protocol) == 0);
	CHECK(protocol == PTHREAD_PRIO_NONE);
	CHECK(pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_NONE) == 0);
	CHECK(pthread_mutexattr_getprioceiling(&attr, &ceiling) == 0);
	CHECK(pthread_mutexattr_setprioceiling(&attr, ceiling) == 0);
	CHECK(pthread_mutexattr_getrobust(&attr, &robust) == 0);
	CHECK(robust == PTHREAD_MUTEX_STALLED);
	CHECK(pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_STALLED) == 0);

	CHECK(pthread_mutex_init(&mutex, &attr) == 0);
	CHECK(pthread_mutex_getprioceiling(&mutex, &old_ceiling) == 0);
	CHECK(old_ceiling == ceiling);
	CHECK(pthread_mutex_setprioceiling(&mutex, ceiling, &old_ceiling) == 0);
	CHECK(old_ceiling == ceiling);
	CHECK(clock_gettime(CLOCK_REALTIME, &now) == 0);
	CHECK(pthread_mutex_timedlock(&mutex, &now) == 0);
	CHECK(pthread_mutex_unlock(&mutex) == 0);
	CHECK(pthread_mutex_consistent(&mutex) == EINVAL);
	CHECK(pthread_mutex_destroy(&mutex) == 0);
	CHECK(pthread_mutexattr_destroy(&attr) == 0);
}

static void test_condition_validation(void)
{
	CHECK(pthread_cond_wait(0, 0) == EINVAL);
}

static void test_rwlock_timed_acquisition(void)
{
	pthread_rwlock_t lock;
	struct timespec now;

	CHECK(pthread_rwlock_init(&lock, 0) == 0);
	CHECK(clock_gettime(CLOCK_REALTIME, &now) == 0);
	CHECK(pthread_rwlock_timedrdlock(&lock, &now) == 0);
	CHECK(pthread_rwlock_unlock(&lock) == 0);
	CHECK(pthread_rwlock_timedwrlock(&lock, &now) == 0);
	CHECK(pthread_rwlock_unlock(&lock) == 0);
	CHECK(pthread_rwlock_destroy(&lock) == 0);
}

int main(void)
{
	test_atfork();
	test_barrier_attributes();
	test_mutex_extensions();
	test_condition_validation();
	test_rwlock_timed_acquisition();
	return fails != 0;
}
