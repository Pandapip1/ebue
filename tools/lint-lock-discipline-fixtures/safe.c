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

/* A release whose own return value is exactly the enclosing function's
 * return value propagates a possible failure to its caller rather than
 * swallowing it -- see LockDisciplineChecker.cpp's isDirectReturnOperand.
 * This is src/thread/pthread_mutex.c's real pthread_mutex_setprioceiling
 * shape: lock, mutate, `return pthread_mutex_unlock(mutex);`, with
 * nothing between the call and the return. */
int setprioceiling_style(mutex_t *mutex) {
  int error = pthread_mutex_lock(mutex);
  if (error)
    return error;
  return pthread_mutex_unlock(mutex);
}

/* cond_wait's real name and its mutex argument's index (1) are both
 * significant here: LockDisciplineChecker.cpp's RequiresHeldOnEntry table
 * matches exactly this (name, index) pair, mirroring
 * src/thread/pthread_cond.c's static cond_wait() helper -- POSIX requires
 * pthread_cond_wait()/pthread_cond_timedwait()'s mutex argument to already
 * be locked on entry, and locked again on every return. Without that
 * table entry, the unlock below would misreport as releasing a lock
 * nobody ever acquired (exactly unsafe.c's unlocked_release, which this
 * is not), and the final relock would misreport as the function leaking
 * a lock it acquired itself, when returning with it held is the entire
 * point. */
int cond_wait(void *cond, mutex_t *mutex, void *absolute) {
  int error = pthread_mutex_unlock(mutex);
  if (error)
    return error;
  /* ...wait for the condition... */
  return pthread_mutex_lock(mutex);
}

/* cond_wait_cleanup mirrors src/thread/pthread_cond.c's real cleanup
 * handler of the same name, registered with pthread_cleanup_push() to run
 * if a thread is cancelled out of cond_wait(): its only job is to
 * reacquire the mutex on the caller's behalf so cond_wait's "always
 * returns with the mutex held" contract holds even under cancellation.
 * The mutex is reached through a struct field inside the void* argument,
 * not a plain parameter -- LockDisciplineChecker.cpp's
 * AcquiresLockForCaller table (matched by function name only) tags
 * whatever region the acquisition below actually resolves to as exempt
 * at the moment it succeeds, rather than needing to know that region in
 * advance. */
struct cond_cleanup {
  mutex_t *mutex;
  int mutex_held;
};

void cond_wait_cleanup(void *argument) {
  struct cond_cleanup *cleanup = argument;
  if (!cleanup->mutex_held) {
    pthread_mutex_lock(cleanup->mutex);
    cleanup->mutex_held = 1;
  }
}
