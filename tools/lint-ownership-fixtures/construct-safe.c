/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef struct { void *opaque[8]; } mutex_t;
typedef struct { void *opaque[12]; } rwlock_t;
typedef struct { void *opaque[8]; } cond_t;
typedef struct { void *opaque[8]; } barrier_t;
typedef struct { volatile int value; } spinlock_t;
typedef struct { void *handle; unsigned magic, named; } semaphore_t;
typedef struct { void *opaque[4]; } mutexattr_t;

int pthread_mutex_init(mutex_t *, const mutexattr_t *);
int pthread_mutex_destroy(mutex_t *);
int pthread_mutex_lock(mutex_t *);
int pthread_mutex_unlock(mutex_t *);
int pthread_mutexattr_init(mutexattr_t *);
int pthread_mutexattr_destroy(mutexattr_t *);
int pthread_rwlock_init(rwlock_t *, const void *);
int pthread_rwlock_destroy(rwlock_t *);
int pthread_rwlock_rdlock(rwlock_t *);
int pthread_rwlock_unlock(rwlock_t *);
int pthread_cond_init(cond_t *, const void *);
int pthread_cond_destroy(cond_t *);
int pthread_cond_signal(cond_t *);
int pthread_barrier_init(barrier_t *, const void *, unsigned);
int pthread_barrier_destroy(barrier_t *);
int pthread_barrier_wait(barrier_t *);
int pthread_spin_init(spinlock_t *, int);
int pthread_spin_destroy(spinlock_t *);
int pthread_spin_lock(spinlock_t *);
int pthread_spin_unlock(spinlock_t *);
int sem_init(semaphore_t *, int, unsigned);
int sem_destroy(semaphore_t *);
int sem_post(semaphore_t *);

static mutex_t static_mutex;

void lazy_static_mutex(void)
{
	pthread_mutex_lock(&static_mutex);
	pthread_mutex_unlock(&static_mutex);
	pthread_mutex_destroy(&static_mutex);
}

void explicit_mutex(void)
{
	mutex_t mutex;
	mutexattr_t attr;
	if (pthread_mutexattr_init(&attr) != 0)
		return;
	if (pthread_mutex_init(&mutex, &attr) == 0) {
		pthread_mutex_lock(&mutex);
		pthread_mutex_unlock(&mutex);
		pthread_mutex_destroy(&mutex);
	}
	pthread_mutexattr_destroy(&attr);
}

void other_constructs(void)
{
	rwlock_t rwlock;
	cond_t condition;
	barrier_t barrier;
	spinlock_t spinlock;
	semaphore_t semaphore;
	if (pthread_rwlock_init(&rwlock, 0) == 0) {
		pthread_rwlock_rdlock(&rwlock);
		pthread_rwlock_unlock(&rwlock);
		pthread_rwlock_destroy(&rwlock);
	}
	if (pthread_cond_init(&condition, 0) == 0) {
		pthread_cond_signal(&condition);
		pthread_cond_destroy(&condition);
	}
	if (pthread_barrier_init(&barrier, 0, 1) == 0) {
		pthread_barrier_wait(&barrier);
		pthread_barrier_destroy(&barrier);
	}
	if (pthread_spin_init(&spinlock, 0) == 0) {
		pthread_spin_lock(&spinlock);
		pthread_spin_unlock(&spinlock);
		pthread_spin_destroy(&spinlock);
	}
	if (sem_init(&semaphore, 0, 0) == 0) {
		sem_post(&semaphore);
		sem_destroy(&semaphore);
	}
}
