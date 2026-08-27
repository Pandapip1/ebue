/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * POSIX semaphores over NT semaphore dispatcher objects. Named semaphore
 * names live in an ordinary file namespace, whose small record names the
 * NT object. unlink() can therefore remove the discoverable name while
 * already-open handles keep the dispatcher object alive.
 */
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

static void object_attributes(const char *ascii, OBJECT_ATTRIBUTES *oa,
	UNICODE_STRING *us, WCHAR *wide, size_t cap)
{
	size_t i, n = strlen(ascii);
	if (n >= cap) n = cap - 1;
	for (i = 0; i < n; i++) wide[i] = (unsigned char)ascii[i];
	wide[n] = 0;
	/* USHORT-safe: n is capped at wide[96]'s caller-supplied capacity. */
	us->Length = (USHORT)(n * sizeof(WCHAR));
	/* USHORT-safe: n + 1 is at most the same 96-WCHAR capacity. */
	us->MaximumLength = (USHORT)((n + 1) * sizeof(WCHAR));
	us->Buffer = wide;
	InitializeObjectAttributes(oa, us, OBJ_CASE_INSENSITIVE | OBJ_INHERIT, 0, 0);
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

int sem_init(sem_t *sem, int pshared, unsigned value)
{
	OBJECT_ATTRIBUTES oa;
	HANDLE h;
	NTSTATUS st;
	(void)pshared;
	if (!sem || value > SEM_VALUE_MAX) { errno = EINVAL; return -1; }
	/* fork() only clones OBJ_INHERIT handles. A process-shared sem_t
	 * stores this handle value in shared memory, and named semaphores
	 * have the same requirement when already open across fork. */
	InitializeObjectAttributes(&oa, 0, OBJ_INHERIT, 0, 0);
	st = NtCreateSemaphore(&h, SEMAPHORE_ALL_ACCESS, &oa, (LONG)value, SEM_VALUE_MAX);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	sem->__handle = h; sem->__magic = SEM_MAGIC; sem->__named = 0;
	return 0;
}

int sem_destroy(sem_t *sem)
{
	if (!valid(sem) || sem->__named) { errno = EINVAL; return -1; }
	NtClose(sem->__handle);
	memset(sem, 0, sizeof *sem);
	return 0;
}

sem_t *sem_open(const char *name, int oflag, ...)
{
	char *path, object[96];
	WCHAR wide[96];
	UNICODE_STRING us;
	OBJECT_ATTRIBUTES oa;
	struct named_sem *entry;
	HANDLE h;
	NTSTATUS st;
	int fd = -1, created = 0, saved;
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
	RtlAcquirePebLock();
	entry = find_path(path);
	if (entry) {
		if ((oflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) {
			RtlReleasePebLock(); free(path); errno = EEXIST; return SEM_FAILED;
		}
		entry->refs++;
		RtlReleasePebLock(); free(path); errno = 0; return &entry->sem;
	}
	RtlReleasePebLock();
	if (ensure_dir(path) < 0) { free(path); return SEM_FAILED; }
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
	if (fd < 0) { free(path); return SEM_FAILED; }
	if (created) {
		snprintf(object, sizeof object, "\\BaseNamedObjects\\ntlibc.sem.%d.%u",
		         (int)getpid(), ++object_sequence);
		object_attributes(object, &oa, &us, wide, sizeof wide / sizeof wide[0]);
		st = NtCreateSemaphore(&h, SEMAPHORE_ALL_ACCESS, &oa, (LONG)value, SEM_VALUE_MAX);
		if (!NT_SUCCESS(st) || write(fd, object, strlen(object) + 1) != (ssize_t)strlen(object) + 1) {
			saved = NT_SUCCESS(st) ? EIO : (__set_errno_status(st), errno);
			close(fd); unlink(path); free(path); errno = saved; return SEM_FAILED;
		}
	} else {
		got = read(fd, object, sizeof object - 1);
		if (got <= 0) { saved = got < 0 ? errno : EIO; close(fd); free(path); errno = saved; return SEM_FAILED; }
		object[got] = 0;
		object_attributes(object, &oa, &us, wide, sizeof wide / sizeof wide[0]);
		st = NtOpenSemaphore(&h, SEMAPHORE_ALL_ACCESS, &oa);
		if (!NT_SUCCESS(st)) { close(fd); free(path); __set_errno_status(st); return SEM_FAILED; }
	}
	close(fd);
	RtlAcquirePebLock();
	entry = free_slot();
	if (!entry) {
		RtlReleasePebLock(); NtClose(h); free(path); errno = EMFILE; return SEM_FAILED;
	}
	entry->sem.__handle = h; entry->sem.__magic = SEM_MAGIC; entry->sem.__named = 1;
	entry->path = path; entry->refs = 1; entry->linked = 1;
	RtlReleasePebLock();
	errno = 0;
	return &entry->sem;
}

int sem_close(sem_t *sem)
{
	struct named_sem *entry;
	if (!valid(sem) || !sem->__named) { errno = EINVAL; return -1; }
	RtlAcquirePebLock();
	entry = find_sem(sem);
	if (!entry || !entry->refs) { RtlReleasePebLock(); errno = EINVAL; return -1; }
	entry->refs--;
	if (!entry->linked && !entry->refs) {
		NtClose(entry->sem.__handle); free(entry->path); memset(entry, 0, sizeof *entry);
	}
	RtlReleasePebLock();
	return 0;
}

int sem_unlink(const char *name)
{
	char *path = sem_path(name);
	struct named_sem *entry;
	int result, saved;
	if (!path) return -1;
	result = unlink(path); saved = errno;
	if (!result) {
		RtlAcquirePebLock();
		entry = find_path(path);
		if (entry) {
			entry->linked = 0;
			if (!entry->refs) {
				NtClose(entry->sem.__handle); free(entry->path); memset(entry, 0, sizeof *entry);
			}
		}
		RtlReleasePebLock();
	}
	free(path); errno = saved; return result;
}

static int wait_handle(sem_t *sem, LARGE_INTEGER *timeout)
{
	NTSTATUS st;
	if (!valid(sem)) { errno = EINVAL; return -1; }
	st = NtWaitForSingleObject(sem->__handle, TRUE, timeout);
	if (st == STATUS_WAIT_0) return 0;
	if (st == STATUS_TIMEOUT) { errno = EAGAIN; return -1; }
	if (st == STATUS_USER_APC || st == STATUS_ALERTED) { errno = EINTR; return -1; }
	return __set_errno_status(st);
}

int sem_trywait(sem_t *sem)
{
	LARGE_INTEGER zero = 0;
	return wait_handle(sem, &zero);
}

int sem_wait(sem_t *sem)
{
	LARGE_INTEGER slice = -500000; /* 50 ms: observe handlers run elsewhere. */
	unsigned long caught;
	int r;
	if (!valid(sem)) { errno = EINVAL; return -1; }
	__pthread_testcancel();
	caught = __sig_caught_count();
	for (;;) {
		r = wait_handle(sem, &slice);
		__pthread_testcancel();
		if (!r) return 0;
		if (errno != EAGAIN) return -1;
		if (__sig_caught_count() != caught) { errno = EINTR; return -1; }
	}
}

int sem_timedwait(sem_t *sem, const struct timespec *abstime)
{
	struct timespec now;
	LARGE_INTEGER rel;
	long long ns;
	unsigned long caught;
	__pthread_testcancel();
	if (sem_trywait(sem) == 0) return 0;
	if (errno != EAGAIN) return -1;
	if (!abstime || abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L) {
		errno = EINVAL; return -1;
	}
	caught = __sig_caught_count();
	for (;;) {
		clock_gettime(CLOCK_REALTIME, &now);
		ns = (long long)(abstime->tv_sec - now.tv_sec) * 1000000000LL + abstime->tv_nsec - now.tv_nsec;
		if (ns <= 0) { errno = ETIMEDOUT; return -1; }
		if (ns > 50000000LL) ns = 50000000LL;
		rel = -(ns / 100);
		if (wait_handle(sem, &rel) == 0) {
			__pthread_testcancel();
			return 0;
		}
		__pthread_testcancel();
		if (errno != EAGAIN) return -1;
		if (__sig_caught_count() != caught) { errno = EINTR; return -1; }
	}
}

int sem_post(sem_t *sem)
{
	NTSTATUS st;
	if (!valid(sem)) { errno = EINVAL; return -1; }
	st = NtReleaseSemaphore(sem->__handle, 1, NULL);
	if (st == STATUS_SEMAPHORE_LIMIT_EXCEEDED) { errno = EOVERFLOW; return -1; }
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

int sem_getvalue(sem_t *sem, int *value)
{
	SEMAPHORE_BASIC_INFORMATION info;
	NTSTATUS st;
	if (!valid(sem) || !value) { errno = EINVAL; return -1; }
	st = NtQuerySemaphore(sem->__handle, SemaphoreBasicInformation,
	                      &info, sizeof info, NULL);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	*value = info.CurrentCount;
	return 0;
}
