/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef struct { void *opaque[8]; } mutex_t;
typedef struct { void *opaque[12]; } rwlock_t;
typedef struct { void *handle; unsigned magic, named; } semaphore_t;

int pthread_mutex_init(mutex_t *, const void *);
int pthread_mutex_destroy(mutex_t *);
int pthread_mutex_lock(mutex_t *);
int pthread_rwlock_destroy(rwlock_t *);
int sem_init(semaphore_t *, int, unsigned);
int sem_destroy(semaphore_t *);
int sem_post(semaphore_t *);

void use_uninitialized(void)
{
	mutex_t mutex;
	pthread_mutex_lock(&mutex); /* ownership-expect: construct-uninitialized */
}

void ignore_initialization_failure(void)
{
	mutex_t mutex;
	pthread_mutex_init(&mutex, 0);
	pthread_mutex_lock(&mutex); /* ownership-expect: construct-init-result */
}

void initialize_twice(void)
{
	mutex_t mutex;
	if (pthread_mutex_init(&mutex, 0) == 0)
		pthread_mutex_init(&mutex, 0); /* ownership-expect: construct-twice */
}

void destroy_twice(void)
{
	semaphore_t semaphore;
	if (sem_init(&semaphore, 0, 0) != 0)
		return;
	if (sem_destroy(&semaphore) == 0)
		sem_destroy(&semaphore); /* ownership-expect: construct-destroyed */
}

void use_after_destroy(void)
{
	semaphore_t semaphore;
	if (sem_init(&semaphore, 0, 0) != 0)
		return;
	if (sem_destroy(&semaphore) == 0)
		sem_post(&semaphore); /* ownership-expect: construct-use-destroyed */
}

void destroy_uninitialized(void)
{
	rwlock_t rwlock;
	pthread_rwlock_destroy(&rwlock); /* ownership-expect: construct-uninitialized */
}
