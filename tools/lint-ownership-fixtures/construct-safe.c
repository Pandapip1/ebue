/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include "../../include/ownership.h"

typedef struct {
  void *opaque[8];
} mutex_t;
typedef struct {
  void *opaque[12];
} rwlock_t;
typedef struct {
  void *opaque[8];
} cond_t;
typedef struct {
  void *opaque[8];
} barrier_t;
typedef struct {
  volatile int value;
} spinlock_t;
typedef struct {
  void *handle;
  unsigned magic, named;
} semaphore_t;
typedef struct {
  void *opaque[4];
} mutexattr_t;

int pthread_mutex_init(mutex_t *construct(mutex) static_handle(mutex),
                       const mutexattr_t *handle(mutexattr));
int pthread_mutex_destroy(mutex_t *destroy(mutex) static_handle(mutex));
int pthread_mutex_lock(mutex_t *handle(mutex) static_handle(mutex));
int pthread_mutex_unlock(mutex_t *handle(mutex) static_handle(mutex));
int pthread_mutexattr_init(mutexattr_t *construct(mutexattr));
int pthread_mutexattr_destroy(mutexattr_t *destroy(mutexattr));
int pthread_rwlock_init(rwlock_t *construct(rwlock) static_handle(rwlock),
                        const void *);
int pthread_rwlock_destroy(rwlock_t *destroy(rwlock) static_handle(rwlock));
int pthread_rwlock_rdlock(rwlock_t *handle(rwlock) static_handle(rwlock));
int pthread_rwlock_unlock(rwlock_t *handle(rwlock) static_handle(rwlock));
int pthread_cond_init(cond_t *construct(condition) static_handle(condition),
                      const void *);
int pthread_cond_destroy(cond_t *destroy(condition) static_handle(condition));
int pthread_cond_signal(cond_t *handle(condition) static_handle(condition));
int pthread_barrier_init(barrier_t *construct(barrier), const void *, unsigned);
int pthread_barrier_destroy(barrier_t *destroy(barrier));
int pthread_barrier_wait(barrier_t *handle(barrier));
int pthread_spin_init(spinlock_t *construct(spinlock), int);
int pthread_spin_destroy(spinlock_t *destroy(spinlock));
int pthread_spin_lock(spinlock_t *handle(spinlock));
int pthread_spin_unlock(spinlock_t *handle(spinlock));
int sem_init(semaphore_t *construct(semaphore), int, unsigned);
int sem_destroy(semaphore_t *destroy(semaphore));
int sem_post(semaphore_t *handle(semaphore));
int sem_trywait(semaphore_t *handle(semaphore));
int pthread_cond_timedwait(cond_t *handle(condition) static_handle(condition),
                           mutex_t *handle(mutex) static_handle(mutex),
                           const void *);

int open_custom(mutex_t *construct(custom));
int inspect_custom(mutex_t *handle(custom));
int close_custom(mutex_t *destroy(custom));

static mutex_t static_mutex;

void lazy_static_mutex(void) {
  pthread_mutex_lock(&static_mutex);
  pthread_mutex_unlock(&static_mutex);
  pthread_mutex_destroy(&static_mutex);
}

void explicit_mutex(void) {
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

void other_constructs(void) {
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

void declaration_driven_custom_names(void) {
  mutex_t object;
  if (open_custom(&object) == 0) {
    inspect_custom(&object);
    close_custom(&object);
  }
}

/* A synchronization object received as a plain pointer PARAMETER -- the
 * exact shape of pthread_cond_wait's own `mutex` argument, or
 * sem_timedwait's `sem`: POSIX requires the CALLER to have already
 * initialized it, in code this per-function analysis cannot see at all
 * (a different translation unit, in the general case). ConstructMap can
 * only ever gain an entry by watching THIS analysis's own
 * pthread_*_init()/sem_init() call directly -- a borrowed parameter's
 * value exists before any code in this function has run, so no code on
 * the callee side could ever satisfy that check. This mirrors exactly
 * why Ownership's release_borrow and Resource's descriptor_borrow (see
 * their own comments in safe.c/resource-safe.c) trust a borrowed
 * pointer's liveness instead of demanding proof this per-function
 * analysis structurally cannot produce. A genuinely never-initialized
 * ON-STACK object (construct-unsafe.c's use_uninitialized/
 * destroy_uninitialized, both `&local`) is real, checkable evidence and
 * remains flagged -- only a borrowed pointer's opaque, cross-boundary
 * provenance is trusted here. */
void lock_via_borrowed_pointer(mutex_t *mutex) {
  pthread_mutex_lock(mutex);
  pthread_mutex_unlock(mutex);
}

void wait_on_borrowed_mutex(cond_t *cond, mutex_t *mutex) {
  pthread_cond_timedwait(cond, mutex, 0);
}

void wait_on_borrowed_semaphore(semaphore_t *sem) { sem_trywait(sem); }
