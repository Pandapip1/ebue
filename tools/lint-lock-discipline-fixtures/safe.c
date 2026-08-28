/* SPDX-License-Identifier: GPL-3.0-or-later */

typedef struct mutex mutex_t;
int pthread_mutex_lock(mutex_t *);
int pthread_mutex_unlock(mutex_t *);

int balanced(mutex_t *mutex) {
  if (pthread_mutex_lock(mutex) != 0)
    return -1;
  if (pthread_mutex_unlock(mutex) != 0)
    __builtin_unreachable();
  return 0;
}
