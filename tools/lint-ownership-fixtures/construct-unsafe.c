/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

typedef struct {
  void *opaque[8];
} mutex_t;
typedef struct {
  void *opaque[12];
} rwlock_t;
typedef struct {
  void *handle;
  unsigned magic, named;
} semaphore_t;

[[ownership_constructs(mutex, 1), ownership_static(mutex, 1)]]
int pthread_mutex_init(mutex_t *, const void *);
[[ownership_destroys(mutex, 1), ownership_static(mutex, 1)]]
int pthread_mutex_destroy(mutex_t *);
[[ownership_requires_handle(mutex, 1), ownership_static(mutex, 1)]]
int pthread_mutex_lock(mutex_t *);
[[ownership_destroys(rwlock, 1), ownership_static(rwlock, 1)]]
int pthread_rwlock_destroy(rwlock_t *);
[[ownership_constructs(semaphore, 1)]] int sem_init(semaphore_t *, int,
                                                    unsigned);
[[ownership_destroys(semaphore, 1)]] int sem_destroy(semaphore_t *);
[[ownership_requires_handle(semaphore, 1)]] int sem_post(semaphore_t *);
[[ownership_constructs(first_class, 1)]] int open_custom(mutex_t *);
[[ownership_requires_handle(second_class, 1)]] int inspect_custom(mutex_t *);

void use_uninitialized(void) {
  mutex_t mutex;
  pthread_mutex_lock(&mutex); /* ownership-expect: construct-uninitialized */
}

void ignore_initialization_failure(void) {
  mutex_t mutex;
  pthread_mutex_init(&mutex, 0);
  pthread_mutex_lock(&mutex); /* ownership-expect: construct-init-result */
}

void initialize_twice(void) {
  mutex_t mutex;
  if (pthread_mutex_init(&mutex, 0) == 0)
    pthread_mutex_init(&mutex, 0); /* ownership-expect: construct-twice */
}

void destroy_twice(void) {
  semaphore_t semaphore;
  if (sem_init(&semaphore, 0, 0) != 0)
    return;
  if (sem_destroy(&semaphore) == 0)
    sem_destroy(&semaphore); /* ownership-expect: construct-destroyed */
}

void use_after_destroy(void) {
  semaphore_t semaphore;
  if (sem_init(&semaphore, 0, 0) != 0)
    return;
  if (sem_destroy(&semaphore) == 0)
    sem_post(&semaphore); /* ownership-expect: construct-use-destroyed */
}

void destroy_uninitialized(void) {
  rwlock_t rwlock;
  pthread_rwlock_destroy(&rwlock); /* ownership-expect: construct-uninitialized */
}

void mismatched_ownership_class(void) {
  mutex_t object;
  if (open_custom(&object) == 0)
    inspect_custom(&object); /* ownership-expect: construct-family-mismatch */
}
