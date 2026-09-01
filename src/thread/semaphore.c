/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * POSIX semaphores over NT semaphore dispatcher objects. Named semaphore
 * names live in an ordinary file namespace, whose small record names the
 * NT object. unlink() can therefore remove the discoverable name while
 * already-open handles keep the dispatcher object alive.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <semaphore.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include "libc.h"
#include "pthread_impl.h"
#include "plat_thread.h"
#include "plat_fd.h"

#define SEM_MAGIC 0x53454d31u
#define NAMED_MAX 64

struct named_sem {
	sem_t sem;
	char *path;
	unsigned refs;
	int linked;
};

static struct named_sem named[NAMED_MAX];
static unsigned object_sequence;
static unsigned unnamed_count;

static int valid(const sem_t *sem)
{
	return sem && sem != SEM_FAILED && sem->__magic == SEM_MAGIC && sem->__handle;
}

static const char *tmpdir(void)
{
	const char *p = getenv("TMPDIR");
	if (!p || !*p) p = getenv("TMP");
	if (!p || !*p) p = getenv("TEMP");
	return p && *p ? p : ".";
}

static int name_char(unsigned char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

__attribute__((ownership_returns(malloc)))
static char *sem_path(const char *name)
{
	const char *component, *dir;
	size_t n, d, i;
	char *path;
	if (!name) { errno = EINVAL; return NULL; }
	component = *name == '/' ? name + 1 : name;
	n = strlen(component);
	if (!n) { errno = EINVAL; return NULL; }
	if (n > NAME_MAX) { errno = ENAMETOOLONG; return NULL; }
	for (i = 0; i < n; i++) if (!name_char((unsigned char)component[i])) {
		errno = EINVAL;
		return NULL;
	}
	dir = tmpdir(); d = strlen(dir);
	if (d + sizeof "/ntlibc-sem/" - 1 + n >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return NULL;
	}
	path = malloc(d + sizeof "/ntlibc-sem/" - 1 + n + 1);
	if (!path) return NULL;
	memcpy(path, dir, d);
	memcpy(path + d, "/ntlibc-sem/", sizeof "/ntlibc-sem/" - 1);
	memcpy(path + d + sizeof "/ntlibc-sem/" - 1, component, n + 1);
	return path;
}

/* path is required: passed to strdup() unconditionally at entry. Same
 * "*slash not fixable here, and not a bug either" residual as
 * src/thread/mqueue.c's own ensure_dir() -- see that function's own
 * comment; this one's real call sites all pass a path built by
 * sem_path(), which likewise always embeds a literal "/ntlibc-sem/"
 * component. */
static int ensure_dir(const char *path) __attribute__((nonnull(1)));
static int ensure_dir(const char *path)
{
	char *copy = strdup(path);
	char *slash;
	int saved;
	if (!copy) return -1;
	slash = strrchr(copy, '/');
	*slash = 0;
	if (mkdir(copy, 0777) < 0 && errno != EEXIST) {
		saved = errno; free(copy); errno = saved; return -1;
	}
	free(copy);
	return 0;
}

/* A named semaphore's filesystem record is its publication point.  Creating
 * the file and filling it cannot be one filesystem operation, so serialize
 * that interval across processes.  Without this lock, two sem_open(O_CREAT)
 * callers can race as follows: one creates the empty record while the other
 * opens and reads it, then both abandon the name.  fork/1-1.c has exactly
 * that shape because parent and child open the same name immediately after
 * the clone.
 *
 * FNV-1a's unsigned wrap is the hash operation itself.  The full path is
 * used, rather than only the POSIX name, so processes with different temp
 * namespaces do not unnecessarily share a lock. */
/* s required: dereferenced unconditionally in the loop condition
 * (`while (*s)`), even for an empty string. */
__wraps static unsigned long long path_hash(const char *s)
    __attribute__((nonnull(1)));
__wraps static unsigned long long path_hash(const char *s)
{
	unsigned long long h = 1469598103934665603ULL;
	while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; }
	return h;
}

static int namespace_lock(const char *path, __plat_handle_t *out)
{
	char name[96];
	unsigned long long hash = path_hash(path);
	int n;

	n = snprintf(name, sizeof name, "\\BaseNamedObjects\\ntlibc.sem.name.%08x%08x",
	         (unsigned)(hash >> 32), (unsigned)hash);
	if (n < 0 || (size_t)n >= sizeof name) {
		if (n >= 0) errno = ENAMETOOLONG;
		return -1;
	}
	return __plat_named_mutant_acquire(name, out);
}

static void namespace_unlock(__plat_handle_t lock)
{
	__plat_named_mutant_release(lock);
}

static struct named_sem *find_path(const char *path)
{
	int i;
	for (i = 0; i < NAMED_MAX; i++)
		if (named[i].path && named[i].linked && !strcmp(named[i].path, path))
			return &named[i];
	return NULL;
}

static struct named_sem *find_sem(sem_t *sem)
{
	int i;
	for (i = 0; i < NAMED_MAX; i++) if (&named[i].sem == sem) return &named[i];
	return NULL;
}

static struct named_sem *free_slot(void)
{
	int i;
	for (i = 0; i < NAMED_MAX; i++) if (!named[i].path) return &named[i];
	return NULL;
}

__attribute__((ownership_constructs(semaphore, 1)))
int sem_init(sem_t *sem, int pshared, unsigned value) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	__plat_handle_t h;
	(void)pshared;
	if (!sem || value > SEM_VALUE_MAX) { errno = EINVAL; return -1; }
	__plat_fast_lock();
	if (unnamed_count == SEM_NSEMS_MAX_) {
		__plat_fast_unlock();
		errno = ENOSPC;
		return -1;
	}
	unnamed_count++;
	__plat_fast_unlock();
	/* fork() only clones OBJ_INHERIT handles. A process-shared sem_t
	 * stores this handle value in shared memory, and named semaphores
	 * have the same requirement when already open across fork. */
	if (__plat_semaphore_create((long)value, SEM_VALUE_MAX, 1, &h) < 0) {
		__plat_fast_lock();
		unnamed_count--;
		__plat_fast_unlock();
		return -1;
	}
	sem->__handle = h; sem->__magic = SEM_MAGIC; sem->__named = 0;
	return 0;
}

__attribute__((ownership_destroys(semaphore, 1)))
int sem_destroy(sem_t *sem)
{
	if (!valid(sem) || sem->__named) { errno = EINVAL; return -1; }
	__plat_close(sem->__handle);
	memset(sem, 0, sizeof *sem);
	__plat_fast_lock();
	unnamed_count--;
	__plat_fast_unlock();
	return 0;
}

sem_t *sem_open(const char *name, int oflag, ...)
{
	char *path, object[96];
	struct named_sem *entry;
	__plat_handle_t h, ns = 0;
	int fd = -1, created = 0, saved, recover = 0, n;
	unsigned value = 0;
	mode_t mode = 0;
	ssize_t got;

	if (oflag & ~(O_CREAT | O_EXCL)) { errno = EINVAL; return SEM_FAILED; }
	if (oflag & O_CREAT) {
		va_list ap;
		va_start(ap, oflag); mode = (mode_t)va_arg(ap, int); value = va_arg(ap, unsigned); va_end(ap);
		if (value > SEM_VALUE_MAX) { errno = EINVAL; return SEM_FAILED; }
	}
	path = sem_path(name);
	if (!path) return SEM_FAILED;
	__plat_fast_lock();
	entry = find_path(path);
	if (entry) {
		if ((oflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) {
			__plat_fast_unlock(); free(path); errno = EEXIST; return SEM_FAILED;
		}
		entry->refs++;
		__plat_fast_unlock(); free(path); errno = 0; return &entry->sem;
	}
	__plat_fast_unlock();
	if (ensure_dir(path) < 0) { free(path); return SEM_FAILED; }
	if (namespace_lock(path, &ns) < 0) { free(path); return SEM_FAILED; }
	/* A same-process opener may have populated the cache while this caller
	 * waited for the cross-process publication lock. */
	__plat_fast_lock();
	entry = find_path(path);
	if (entry) {
		if ((oflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) {
			__plat_fast_unlock(); namespace_unlock(ns); free(path);
			errno = EEXIST; return SEM_FAILED;
		}
		entry->refs++;
		__plat_fast_unlock(); namespace_unlock(ns); free(path);
		errno = 0; return &entry->sem;
	}
	__plat_fast_unlock();

retry_record:
	created = 0;
	if (oflag & O_CREAT) {
		/* The record is implementation metadata, not the semaphore's
		 * permission object. Keeping it owner-writable is necessary on
		 * this filesystem mapping: clearing owner-write maps to NT's
		 * read-only attribute and would make the POSIX-required
		 * sem_unlink() fail merely because mode was 0444 or 0. */
		(void)mode;
		fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
		if (fd >= 0) created = 1;
		else if (errno == EEXIST && !(oflag & O_EXCL)) fd = open(path, O_RDONLY, 0);
	} else fd = open(path, O_RDONLY, 0);
	if (fd < 0) { saved = errno; goto fail_locked; }
	if (created) {
		int create_result;
		n = snprintf(object, sizeof object, "\\BaseNamedObjects\\ntlibc.sem.%d.%u",
		         (int)getpid(), ++object_sequence);
		if (n < 0 || (size_t)n >= sizeof object) {
			saved = n < 0 ? errno : ENAMETOOLONG;
			(void)close(fd);
			(void)unlink(path);
			goto fail_locked;
		}
		create_result = __plat_named_semaphore_create(object, (long)value, SEM_VALUE_MAX, &h);
		if (create_result < 0 ||
		    write(fd, object, (size_t)n + 1) != (ssize_t)n + 1) {
			saved = create_result == 0 ? EIO : errno;
			(void)close(fd); (void)unlink(path); goto fail_locked;
		}
	} else {
		int open_result;
		got = read(fd, object, sizeof object - 1);
		if (got <= 0) {
			saved = got < 0 ? errno : EIO;
			(void)close(fd);
			/* A creator can die after publishing the record but before
			 * filling it.  O_CREAT without O_EXCL owns recovery while the
			 * namespace lock proves nobody can still be publishing it. */
			if ((oflag & O_CREAT) && !(oflag & O_EXCL) && !recover) {
				recover = 1;
				if (unlink(path) == 0) goto retry_record;
				saved = errno;
			}
			goto fail_locked;
		}
		object[got] = 0;
		open_result = __plat_named_semaphore_open(object, &h);
		if (open_result < 0) {
			(void)close(fd);
			if ((oflag & O_CREAT) && !(oflag & O_EXCL) && !recover &&
			    open_result == -2) {
				recover = 1;
				if (unlink(path) == 0) goto retry_record;
				saved = errno;
			} else {
				if (open_result == -2) errno = ENOENT;
				saved = errno;
			}
			goto fail_locked;
		}
	}
	(void)close(fd);
	__plat_fast_lock();
	entry = free_slot();
	if (!entry) {
		__plat_fast_unlock(); __plat_close(h);
		if (created) (void)unlink(path);
		saved = EMFILE; goto fail_locked;
	}
	entry->sem.__handle = h; entry->sem.__magic = SEM_MAGIC; entry->sem.__named = 1;
	entry->path = path; entry->refs = 1; entry->linked = 1;
	__plat_fast_unlock();
	namespace_unlock(ns);
	errno = 0;
	return &entry->sem;

fail_locked:
	namespace_unlock(ns);
	free(path);
	errno = saved;
	return SEM_FAILED;
}

int sem_close(sem_t *sem)
{
	struct named_sem *entry;
	if (!valid(sem) || !sem->__named) { errno = EINVAL; return -1; }
	__plat_fast_lock();
	entry = find_sem(sem);
	if (!entry || !entry->refs) { __plat_fast_unlock(); errno = EINVAL; return -1; }
	entry->refs--;
	if (!entry->linked && !entry->refs) {
		__plat_close(entry->sem.__handle); free(entry->path); memset(entry, 0, sizeof *entry);
	}
	__plat_fast_unlock();
	return 0;
}

int sem_unlink(const char *name)
{
	char *path = sem_path(name);
	struct named_sem *entry;
	__plat_handle_t ns = 0;
	int result, saved;
	if (!path) return -1;
	if (ensure_dir(path) < 0) { free(path); return -1; }
	if (namespace_lock(path, &ns) < 0) { free(path); return -1; }
	result = unlink(path); saved = errno;
	if (!result) {
		__plat_fast_lock();
		entry = find_path(path);
		if (entry) {
			entry->linked = 0;
			if (!entry->refs) {
				__plat_close(entry->sem.__handle); free(entry->path); memset(entry, 0, sizeof *entry);
			}
		}
		__plat_fast_unlock();
	}
	namespace_unlock(ns);
	free(path); errno = saved; return result;
}

static int wait_handle(sem_t *sem, long long ticks)
{
	int r;
	if (!valid(sem)) { errno = EINVAL; return -1; }
	r = __plat_wait_one(sem->__handle, 1, 1, ticks);
	if (r == __PLAT_WAIT_OK) return 0;
	if (r == __PLAT_WAIT_TIMEOUT) { errno = EAGAIN; return -1; }
	if (r == __PLAT_WAIT_INTR) { errno = EINTR; return -1; }
	return -1; /* __PLAT_WAIT_ERROR: errno already set */
}

static int restartable_interruption(unsigned long *caught, // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
	unsigned long *restarted)
{
	unsigned long now_caught = __sig_thread_caught_count();
	unsigned long now_restarted = __sig_thread_restart_count();
	unsigned long delivered = now_caught - *caught;

	if (!delivered) return 0;
	if (delivered == now_restarted - *restarted) {
		*caught = now_caught;
		*restarted = now_restarted;
		return 1;
	}
	return -1;
}

__attribute__((ownership_requires_handle(semaphore, 1)))
int sem_trywait(sem_t *sem)
{
	return wait_handle(sem, 0);
}

__attribute__((ownership_requires_handle(semaphore, 1)))
int sem_wait(sem_t *sem)
{
	const long long slice = -500000; /* 50 ms: observe handlers run elsewhere. */
	unsigned long caught;
	unsigned long restarted;
	int r;
	if (!valid(sem)) { errno = EINVAL; return -1; }
	__pthread_testcancel();
	caught = __sig_thread_caught_count();
	restarted = __sig_thread_restart_count();
	for (;;) {
		r = wait_handle(sem, slice);
		__pthread_testcancel();
		/* Cross-process delivery queues the signal for an application
		 * thread; the listener must not run user handlers itself.  NT
		 * semaphore waits are not part of that delivery path, so bounded
		 * waits must explicitly run pending handlers before deciding
		 * whether this operation was interrupted. */
		__sig_drain_pending();
		if (!r) return 0;
		if (errno == EINTR) {
			/* wait_handle() reports EINTR for ANY __PLAT_WAIT_INTR wake,
			 * and a deferred pthread_cancel() causes exactly such a wake
			 * when the cancellation cannot be delivered yet
			 * (__pthread_cancel_defer_enter() is still active) -- see
			 * src/thread/pthread_cancel.c's redirect_async_cancel().
			 * That wake delivers no signal at all, so
			 * restartable_interruption() reports zero delivered here,
			 * not a negative count; only a genuine, non-restarting
			 * signal earns the EINTR this call reports to its caller. */
			if (restartable_interruption(&caught, &restarted) >= 0) continue;
			return -1;
		}
		if (errno != EAGAIN) return -1;
		r = restartable_interruption(&caught, &restarted);
		if (r < 0) { errno = EINTR; return -1; }
	}
}

__attribute__((ownership_requires_handle(semaphore, 1)))
int sem_timedwait(sem_t *sem, const struct timespec *abstime)
{
	struct timespec now;
	long long ticks;
	unsigned long caught;
	unsigned long restarted;
	int r;
	__pthread_testcancel();
	if (sem_trywait(sem) == 0) return 0;
	if (errno != EAGAIN) return -1;
	if (!abstime || abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L) {
		errno = EINVAL; return -1;
	}
	caught = __sig_thread_caught_count();
	restarted = __sig_thread_restart_count();
	for (;;) {
		clock_gettime(CLOCK_REALTIME, &now);
		ticks = __timespec_diff_ticks(abstime->tv_sec, abstime->tv_nsec,
			now.tv_sec, now.tv_nsec);
		if (ticks <= 0) { errno = ETIMEDOUT; return -1; }
		if (ticks > 500000) ticks = 500000;
		r = wait_handle(sem, -ticks);
		__pthread_testcancel();
		__sig_drain_pending();
		if (!r) return 0;
		if (errno == EINTR) {
			/* See sem_wait()'s comment on this same check: a deferred
			 * pthread_cancel()'s wake also delivers no signal, so a
			 * zero (not negative) restartable_interruption() result
			 * must retry too. */
			if (restartable_interruption(&caught, &restarted) >= 0) continue;
			return -1;
		}
		if (errno != EAGAIN) return -1;
		r = restartable_interruption(&caught, &restarted);
		if (r < 0) { errno = EINTR; return -1; }
	}
}

__attribute__((ownership_requires_handle(semaphore, 1)))
int sem_post(sem_t *sem)
{
	if (!valid(sem)) { errno = EINVAL; return -1; }
	return __plat_semaphore_post(sem->__handle);
}

__attribute__((ownership_requires_handle(semaphore, 1)))
int sem_getvalue(sem_t *sem, int *value)
{
	if (!valid(sem) || !value) { errno = EINVAL; return -1; }
	return __plat_semaphore_getvalue(sem->__handle, value);
}

// NOLINTEND(misc-include-cleaner)
