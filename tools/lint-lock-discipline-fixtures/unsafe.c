/* SPDX-License-Identifier: GPL-3.0-or-later */

typedef struct mutex mutex_t;
int pthread_mutex_lock(mutex_t *);
int pthread_mutex_unlock(mutex_t *);
int pthread_mutex_destroy(mutex_t *);
int pthread_cond_wait(void *, mutex_t *);

int unlocked_release(mutex_t *mutex) {
  return pthread_mutex_unlock(mutex); /* lock-discipline-expect */
}

int missing_release(mutex_t *mutex) {
  if (pthread_mutex_lock(mutex) != 0)
    return -1;
  return 0; /* lock-discipline-expect */
}

int wait_without_lock(void *condition, mutex_t *mutex) {
  return pthread_cond_wait(condition, mutex); /* lock-discipline-expect */
}

int destroy_held(mutex_t *mutex) {
  int result;
  if (pthread_mutex_lock(mutex) != 0)
    return -1;
  result = pthread_mutex_destroy(mutex); /* lock-discipline-expect */
  if (pthread_mutex_unlock(mutex) != 0)
    __builtin_unreachable();
  return result;
}
