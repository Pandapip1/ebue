/* SPDX-FileCopyrightText: (C) 2026 Gavin John */
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

/* Proves the hand-off exemption safe.c's cond_wait/cond_wait_cleanup
 * fixtures rely on is scoped to the one designated region
 * (RequiresHeldOnEntry's argument index 1), not the whole function: a
 * *second*, ordinary mutex this same cond_wait leaks must still be
 * reported. If LockDisciplineChecker.cpp's exemption were ever
 * broadened to skip the whole function by name instead of tagging the
 * specific MemRegion, this finding would silently vanish along with the
 * real one. */
int cond_wait(void *cond, mutex_t *mutex, mutex_t *extra) {
  if (pthread_mutex_lock(extra) != 0)
    return -1;
  return 0; /* lock-discipline-expect */
}
