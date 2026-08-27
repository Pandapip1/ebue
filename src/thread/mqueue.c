/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * POSIX message queues backed by unlinkable regular files.  Three named NT
 * semaphores accompany each generation of a queue: a binary state lock, an
 * available-message count, and an available-slot count.  Their names live in
 * the file header, so unrelated processes can reopen the queue; if every
 * process closes it while its pathname remains, mq_open() recreates the NT
 * objects from the authoritative counts in that header.
 *
 * Queue descriptors are ordinary close-on-exec file descriptors plus the
 * semaphore handles in mqds[].  That gives fork() the right behavior without
 * teaching the process launcher a private descriptor format.  close() calls
 * __mq_fd_closed() so using close(mqdes), although not the POSIX interface,
 * cannot leave a stale descriptor association or notification registration.
 */
#include <mqueue.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include "libc.h"

#define MQ_MAGIC 0x4e544d51u
#define MQ_VERSION 1
#define MQ_MAXMSG_LIMIT 256
#define MQ_MSGSIZE_LIMIT 65536
#define MQ_DEFAULT_MAXMSG 10
#define MQ_DEFAULT_MSGSIZE 8192
#define MQ_DESC_MAGIC 0x4d514431u

struct mq_header {
	unsigned magic;
	unsigned version;
	unsigned maxmsg;
	unsigned msgsize;
	unsigned curmsgs;
	unsigned receive_waiters;
	unsigned long long sequence;
	int notify_active;
	int notify_kind;
	int notify_pid;
	int notify_fd;
	int notify_signo;
	union sigval notify_value;
	char lock_name[112];
	char items_name[112];
	char spaces_name[112];
};

struct mq_slot {
	unsigned used;
	unsigned priority;
	unsigned length;
	unsigned reserved;
	unsigned long long sequence;
};

struct mq_desc {
	unsigned magic;
	HANDLE file;
	HANDLE lock;
	HANDLE items;
	HANDLE spaces;
	unsigned maxmsg;
	unsigned msgsize;
};

static struct mq_desc mqds[FD_MAX];
static unsigned object_sequence;

static const char *mq_tmpdir(void)
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

static char *mq_path(const char *name)
{
	const char *component, *dir;
	size_t n, d, i;
	char *path;
	if (!name) { errno = EINVAL; return NULL; }
	if (strlen(name) >= PATH_MAX) { errno = ENAMETOOLONG; return NULL; }
	component = *name == '/' ? name + 1 : name;
	n = strlen(component);
	if (!n || !strcmp(component, ".") || !strcmp(component, "..")) {
		errno = EINVAL; return NULL;
	}
	if (n > NAME_MAX) { errno = ENAMETOOLONG; return NULL; }
	for (i = 0; i < n; i++) if (!name_char((unsigned char)component[i])) {
		errno = EINVAL; return NULL;
	}
	dir = mq_tmpdir(); d = strlen(dir);
	if (d + sizeof "/ntlibc-mq/" - 1 + n >= PATH_MAX) {
		errno = ENAMETOOLONG; return NULL;
	}
	path = malloc(d + sizeof "/ntlibc-mq/" - 1 + n + 1);
	if (!path) return NULL;
	memcpy(path, dir, d);
	memcpy(path + d, "/ntlibc-mq/", sizeof "/ntlibc-mq/" - 1);
	memcpy(path + d + sizeof "/ntlibc-mq/" - 1, component, n + 1);
	return path;
}

static int ensure_dir(const char *path)
{
	char *copy = strdup(path), *slash;
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

static unsigned long long path_hash(const char *s)
{
	unsigned long long h = 1469598103934665603ULL;
	while (*s) { h ^= (unsigned char)*s++; h *= 1099511628211ULL; }
	return h;
}

static void object_attributes(const char *ascii, OBJECT_ATTRIBUTES *oa,
	UNICODE_STRING *us, WCHAR *wide, size_t cap)
{
	size_t i, n = strlen(ascii);
	if (n >= cap) n = cap - 1;
	for (i = 0; i < n; i++) wide[i] = (unsigned char)ascii[i];
	wide[n] = 0;
	us->Length = (USHORT)(n * sizeof(WCHAR));
	us->MaximumLength = (USHORT)((n + 1) * sizeof(WCHAR));
	us->Buffer = wide;
	InitializeObjectAttributes(oa, us, OBJ_CASE_INSENSITIVE | OBJ_INHERIT, 0, 0);
}

static int create_sem(const char *name, LONG initial, LONG maximum, HANDLE *out)
{
	OBJECT_ATTRIBUTES oa;
	UNICODE_STRING us;
	WCHAR wide[128];
	NTSTATUS st;
	object_attributes(name, &oa, &us, wide, sizeof wide / sizeof wide[0]);
	st = NtCreateSemaphore(out, SEMAPHORE_ALL_ACCESS, &oa, initial, maximum);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

static int take(HANDLE h)
{
	NTSTATUS st = NtWaitForSingleObject(h, FALSE, NULL);
	if (st == STATUS_WAIT_0) return 0;
	return __set_errno_status(st);
}

static void give(HANDLE h)
{
	NtReleaseSemaphore(h, 1, NULL);
}

static int raw_io(HANDLE h, void *buf, size_t len, off_t off, int write_op)
{
	unsigned char *p = buf;
	while (len) {
		IO_STATUS_BLOCK io;
		LARGE_INTEGER pos = off;
		ULONG part = len > 0x7fffffff ? 0x7fffffff : (ULONG)len;
		NTSTATUS st;
		io.Information = 0;
		if (write_op)
			st = NtWriteFile(h, 0, 0, 0, &io, p, part, &pos, 0);
		else
			st = NtReadFile(h, 0, 0, 0, &io, p, part, &pos, 0);
		if (st == STATUS_PENDING) {
			NtWaitForSingleObject(h, FALSE, NULL);
			st = io.Status;
		}
		if (!NT_SUCCESS(st)) return __set_errno_status(st);
		if (!io.Information) { errno = EIO; return -1; }
		p += io.Information;
		off += io.Information;
		len -= io.Information;
	}
	return 0;
}

static int raw_read(HANDLE h, void *buf, size_t len, off_t off)
{
	return raw_io(h, buf, len, off, 0);
}

static int raw_write(HANDLE h, const void *buf, size_t len, off_t off)
{
	return raw_io(h, (void *)buf, len, off, 1);
}

static off_t slot_offset(const struct mq_desc *d, unsigned slot)
{
	return (off_t)sizeof(struct mq_header) +
	       (off_t)slot * (sizeof(struct mq_slot) + d->msgsize);
}

static struct mq_desc *get_desc(mqd_t mqdes)
{
	struct mq_desc *d;
	if (mqdes < 0 || mqdes >= FD_MAX) { errno = EBADF; return NULL; }
	d = &mqds[mqdes];
	if (d->magic != MQ_DESC_MAGIC || !d->file || !__fds[mqdes].h ||
	    __fds[mqdes].h != d->file) { errno = EBADF; return NULL; }
	return d;
}

static int read_header(struct mq_desc *d, struct mq_header *h)
{
	if (raw_read(d->file, h, sizeof *h, 0) < 0) return -1;
	if (h->magic != MQ_MAGIC || h->version != MQ_VERSION ||
	    h->maxmsg != d->maxmsg || h->msgsize != d->msgsize) {
		errno = EIO; return -1;
	}
	return 0;
}

static int attr_valid(const struct mq_attr *a)
{
	return a && a->mq_maxmsg > 0 && a->mq_maxmsg <= MQ_MAXMSG_LIMIT &&
	       a->mq_msgsize > 0 && a->mq_msgsize <= MQ_MSGSIZE_LIMIT;
}

mqd_t mq_open(const char *name, int oflag, ...)
{
	char *path = NULL;
	char nsname[96];
	unsigned long long hash;
	struct mq_header h;
	struct mq_attr supplied, *attr = NULL;
	struct mq_desc *d;
	HANDLE ns = 0, lock = 0, items = 0, spaces = 0;
	int fd = -1, created = 0, saved = 0, access = oflag & O_ACCMODE;
	mode_t mode = 0;
	size_t file_size;

	if (access != O_RDONLY && access != O_WRONLY && access != O_RDWR) {
		errno = EINVAL; return (mqd_t)-1;
	}
	if (oflag & ~(O_ACCMODE | O_CREAT | O_EXCL | O_NONBLOCK)) {
		errno = EINVAL; return (mqd_t)-1;
	}
	if (oflag & O_CREAT) {
		va_list ap;
		va_start(ap, oflag);
		mode = (mode_t)va_arg(ap, int);
		attr = va_arg(ap, struct mq_attr *);
		va_end(ap);
		if (attr) supplied = *attr;
		if (attr && !attr_valid(&supplied)) { errno = EINVAL; return (mqd_t)-1; }
	}
	path = mq_path(name);
	if (!path) return (mqd_t)-1;
	if (ensure_dir(path) < 0) goto fail;
	hash = path_hash(path);
	snprintf(nsname, sizeof nsname, "\\BaseNamedObjects\\ntlibc.mq.name.%08x%08x",
	         (unsigned)(hash >> 32), (unsigned)hash);
	if (create_sem(nsname, 1, 1, &ns) < 0 || take(ns) < 0) goto fail;

	if (oflag & O_CREAT) {
		(void)mode;
		fd = open(path, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
		if (fd >= 0) created = 1;
		else if (errno == EEXIST && !(oflag & O_EXCL))
			fd = open(path, O_RDWR | O_CLOEXEC, 0);
	} else {
		fd = open(path, O_RDWR | O_CLOEXEC, 0);
	}
	if (fd < 0) goto fail_locked;

	if (created) {
		memset(&h, 0, sizeof h);
		h.magic = MQ_MAGIC;
		h.version = MQ_VERSION;
		h.maxmsg = attr ? (unsigned)supplied.mq_maxmsg : MQ_DEFAULT_MAXMSG;
		h.msgsize = attr ? (unsigned)supplied.mq_msgsize : MQ_DEFAULT_MSGSIZE;
		h.sequence = 1;
		object_sequence++;
		snprintf(h.lock_name, sizeof h.lock_name,
		         "\\BaseNamedObjects\\ntlibc.mq.%d.%u.lock", (int)getpid(), object_sequence);
		snprintf(h.items_name, sizeof h.items_name,
		         "\\BaseNamedObjects\\ntlibc.mq.%d.%u.items", (int)getpid(), object_sequence);
		snprintf(h.spaces_name, sizeof h.spaces_name,
		         "\\BaseNamedObjects\\ntlibc.mq.%d.%u.spaces", (int)getpid(), object_sequence);
		file_size = sizeof h + (size_t)h.maxmsg * (sizeof(struct mq_slot) + h.msgsize);
		if (ftruncate(fd, (off_t)file_size) < 0 || raw_write(__fds[fd].h, &h, sizeof h, 0) < 0)
			goto fail_created;
	} else if (raw_read(__fds[fd].h, &h, sizeof h, 0) < 0 ||
	           h.magic != MQ_MAGIC || h.version != MQ_VERSION ||
	           !h.maxmsg || h.maxmsg > MQ_MAXMSG_LIMIT ||
	           !h.msgsize || h.msgsize > MQ_MSGSIZE_LIMIT) {
		if (!errno) errno = EIO;
		goto fail_fd;
	}

	if (create_sem(h.lock_name, 1, 1, &lock) < 0 || take(lock) < 0) goto fail_fd;
	/* The state lock makes these counts an atomic snapshot when all NT
	 * objects had disappeared and are being reconstructed. */
	if (raw_read(__fds[fd].h, &h, sizeof h, 0) < 0) goto fail_qlocked;
	if (create_sem(h.items_name, (LONG)h.curmsgs, (LONG)h.maxmsg, &items) < 0 ||
	    create_sem(h.spaces_name, (LONG)(h.maxmsg - h.curmsgs), (LONG)h.maxmsg, &spaces) < 0)
		goto fail_qlocked;
	give(lock);
	give(ns);
	NtClose(ns);

	d = &mqds[fd];
	memset(d, 0, sizeof *d);
	d->magic = MQ_DESC_MAGIC;
	d->file = __fds[fd].h;
	d->lock = lock;
	d->items = items;
	d->spaces = spaces;
	d->maxmsg = h.maxmsg;
	d->msgsize = h.msgsize;
	__fds[fd].flags = (unsigned)access | (oflag & O_NONBLOCK) | O_CLOEXEC;
	free(path);
	errno = 0;
	return fd;

fail_qlocked:
	give(lock);
fail_fd:
	saved = errno;
	if (fd >= 0) close(fd);
	if (created) unlink(path);
	goto fail_locked_saved;
fail_created:
	saved = errno;
	close(fd); fd = -1;
	unlink(path);
	goto fail_locked_saved;
fail_locked:
	saved = errno;
fail_locked_saved:
	give(ns);
fail:
	if (!saved) saved = errno;
	if (spaces) NtClose(spaces);
	if (items) NtClose(items);
	if (lock) NtClose(lock);
	if (ns) NtClose(ns);
	free(path);
	errno = saved;
	return (mqd_t)-1;
}

static int wait_count(struct mq_desc *d, HANDLE count, int nonblock,
	const struct timespec *abstime, int timed, int receiver)
{
	LARGE_INTEGER zero = 0, slice;
	struct timespec now;
	struct mq_header h;
	unsigned long caught = __sig_caught_count();
	int registered = 0;
	NTSTATUS st = NtWaitForSingleObject(count, TRUE, &zero);
	if (st == STATUS_WAIT_0) return 0;
	if (st != STATUS_TIMEOUT && st != STATUS_ALERTED && st != STATUS_USER_APC)
		return __set_errno_status(st);
	if (nonblock) { errno = EAGAIN; return -1; }
	if (timed && (!abstime || abstime->tv_nsec < 0 || abstime->tv_nsec >= 1000000000L)) {
		errno = EINVAL; return -1;
	}
	if (receiver) {
		if (take(d->lock) < 0) return -1;
		if (read_header(d, &h) < 0) { give(d->lock); return -1; }
		h.receive_waiters++;
		if (raw_write(d->file, &h, sizeof h, 0) < 0) { give(d->lock); return -1; }
		give(d->lock);
		registered = 1;
	}
	for (;;) {
		if (timed) {
			long long ns;
			clock_gettime(CLOCK_REALTIME, &now);
			ns = (long long)(abstime->tv_sec - now.tv_sec) * 1000000000LL +
			     abstime->tv_nsec - now.tv_nsec;
			if (ns <= 0) { errno = ETIMEDOUT; break; }
			if (ns > 50000000LL) ns = 50000000LL;
			slice = -(ns / 100);
		} else slice = -500000;
		st = NtWaitForSingleObject(count, TRUE, &slice);
		if (st == STATUS_WAIT_0) break;
		if (st != STATUS_TIMEOUT && st != STATUS_ALERTED && st != STATUS_USER_APC) {
			__set_errno_status(st); break;
		}
		if (__sig_caught_count() != caught) { errno = EINTR; break; }
	}
	if (registered) {
		int saved = errno;
		if (take(d->lock) == 0) {
			if (read_header(d, &h) == 0 && h.receive_waiters) {
				h.receive_waiters--;
				raw_write(d->file, &h, sizeof h, 0);
			}
			give(d->lock);
		}
		errno = saved;
	}
	return st == STATUS_WAIT_0 ? 0 : -1;
}

int mq_timedsend(mqd_t mqdes, const char *msg, size_t len, unsigned prio,
	const struct timespec *abstime)
{
	struct mq_desc *d = get_desc(mqdes);
	struct mq_header h;
	struct mq_slot s;
	unsigned i, free_slot = d ? d->maxmsg : 0;
	int notify = 0, notify_kind = 0, notify_pid = 0, notify_signo = 0;
	union sigval notify_value;
	if (!d) return -1;
	if ((__fds[mqdes].flags & O_ACCMODE) == O_RDONLY) { errno = EBADF; return -1; }
	if (len > d->msgsize) { errno = EMSGSIZE; return -1; }
	if (prio >= MQ_PRIO_MAX) { errno = EINVAL; return -1; }
	if (wait_count(d, d->spaces, __fds[mqdes].flags & O_NONBLOCK,
	               abstime, abstime != NULL, 0) < 0) return -1;
	if (take(d->lock) < 0) { give(d->spaces); return -1; }
	if (read_header(d, &h) < 0) goto rollback;
	for (i = 0; i < d->maxmsg; i++) {
		if (raw_read(d->file, &s, sizeof s, slot_offset(d, i)) < 0) goto rollback;
		if (!s.used) { free_slot = i; break; }
	}
	if (free_slot == d->maxmsg) { errno = EIO; goto rollback; }
	memset(&s, 0, sizeof s);
	s.used = 1; s.priority = prio; s.length = (unsigned)len; s.sequence = h.sequence++;
	if (len && raw_write(d->file, msg, len,
	                     slot_offset(d, free_slot) + sizeof s) < 0) goto rollback;
	if (raw_write(d->file, &s, sizeof s, slot_offset(d, free_slot)) < 0) goto rollback;
	if (!h.curmsgs && h.notify_active && !h.receive_waiters) {
		notify = 1; notify_kind = h.notify_kind; notify_pid = h.notify_pid;
		notify_signo = h.notify_signo; notify_value = h.notify_value;
		h.notify_active = 0;
	}
	h.curmsgs++;
	if (raw_write(d->file, &h, sizeof h, 0) < 0) goto rollback;
	give(d->lock);
	give(d->items);
	if (notify && notify_kind == SIGEV_SIGNAL)
		(void)sigqueue((pid_t)notify_pid, notify_signo, notify_value);
	return 0;
rollback:
	give(d->lock);
	give(d->spaces);
	return -1;
}

int mq_send(mqd_t mqdes, const char *msg, size_t len, unsigned prio)
{
	return mq_timedsend(mqdes, msg, len, prio, NULL);
}

ssize_t mq_timedreceive(mqd_t mqdes, char *msg, size_t len, unsigned *prio,
	const struct timespec *abstime)
{
	struct mq_desc *d = get_desc(mqdes);
	struct mq_header h;
	struct mq_slot s, best;
	unsigned i, selected = d ? d->maxmsg : 0;
	if (!d) return -1;
	if ((__fds[mqdes].flags & O_ACCMODE) == O_WRONLY) { errno = EBADF; return -1; }
	if (len < d->msgsize) { errno = EMSGSIZE; return -1; }
	if (wait_count(d, d->items, __fds[mqdes].flags & O_NONBLOCK,
	               abstime, abstime != NULL, 1) < 0) return -1;
	if (take(d->lock) < 0) { give(d->items); return -1; }
	if (read_header(d, &h) < 0) goto rollback;
	memset(&best, 0, sizeof best);
	for (i = 0; i < d->maxmsg; i++) {
		if (raw_read(d->file, &s, sizeof s, slot_offset(d, i)) < 0) goto rollback;
		if (s.used && (selected == d->maxmsg || s.priority > best.priority ||
		    (s.priority == best.priority && s.sequence < best.sequence))) {
			best = s; selected = i;
		}
	}
	if (selected == d->maxmsg || !h.curmsgs || best.length > d->msgsize) {
		errno = EIO; goto rollback;
	}
	if (best.length && raw_read(d->file, msg, best.length,
	                            slot_offset(d, selected) + sizeof best) < 0) goto rollback;
	memset(&s, 0, sizeof s);
	if (raw_write(d->file, &s, sizeof s, slot_offset(d, selected)) < 0) goto rollback;
	h.curmsgs--;
	if (raw_write(d->file, &h, sizeof h, 0) < 0) goto rollback;
	give(d->lock);
	give(d->spaces);
	if (prio) *prio = best.priority;
	return (ssize_t)best.length;
rollback:
	give(d->lock);
	give(d->items);
	return -1;
}

ssize_t mq_receive(mqd_t mqdes, char *msg, size_t len, unsigned *prio)
{
	return mq_timedreceive(mqdes, msg, len, prio, NULL);
}

int mq_getattr(mqd_t mqdes, struct mq_attr *attr)
{
	struct mq_desc *d = get_desc(mqdes);
	struct mq_header h;
	if (!d) return -1;
	if (!attr) { errno = EINVAL; return -1; }
	if (take(d->lock) < 0) return -1;
	if (read_header(d, &h) < 0) { give(d->lock); return -1; }
	memset(attr, 0, sizeof *attr);
	attr->mq_flags = __fds[mqdes].flags & O_NONBLOCK;
	attr->mq_maxmsg = h.maxmsg;
	attr->mq_msgsize = h.msgsize;
	attr->mq_curmsgs = h.curmsgs;
	give(d->lock);
	return 0;
}

int mq_setattr(mqd_t mqdes, const struct mq_attr *attr, struct mq_attr *old)
{
	struct mq_desc *d = get_desc(mqdes);
	if (!d) return -1;
	if (!attr) { errno = EINVAL; return -1; }
	if (old && mq_getattr(mqdes, old) < 0) return -1;
	__fds[mqdes].flags = (__fds[mqdes].flags & ~O_NONBLOCK) |
	                       (attr->mq_flags & O_NONBLOCK);
	return 0;
}

int mq_notify(mqd_t mqdes, const struct sigevent *event)
{
	struct mq_desc *d = get_desc(mqdes);
	struct mq_header h;
	if (!d) return -1;
	if (event && event->sigev_notify != SIGEV_SIGNAL && event->sigev_notify != SIGEV_NONE) {
		errno = EINVAL; return -1;
	}
	if (event && event->sigev_notify == SIGEV_SIGNAL &&
	    (event->sigev_signo < 0 || event->sigev_signo >= _NSIG)) {
		errno = EINVAL; return -1;
	}
	if (take(d->lock) < 0) return -1;
	if (read_header(d, &h) < 0) { give(d->lock); return -1; }
	if (!event) {
		if (h.notify_active && h.notify_pid == (int)getpid()) h.notify_active = 0;
	} else {
		if (h.notify_active) { give(d->lock); errno = EBUSY; return -1; }
		h.notify_active = 1;
		h.notify_kind = event->sigev_notify;
		h.notify_pid = (int)getpid();
		h.notify_fd = mqdes;
		h.notify_signo = event->sigev_signo;
		h.notify_value = event->sigev_value;
	}
	if (raw_write(d->file, &h, sizeof h, 0) < 0) { give(d->lock); return -1; }
	give(d->lock);
	return 0;
}

void __mq_fd_closed(int fd)
{
	struct mq_desc *d;
	struct mq_header h;
	if (fd < 0 || fd >= FD_MAX) return;
	d = &mqds[fd];
	if (d->magic != MQ_DESC_MAGIC || d->file != __fds[fd].h) return;
	if (take(d->lock) == 0) {
		if (read_header(d, &h) == 0 && h.notify_active &&
		    h.notify_pid == (int)getpid() && h.notify_fd == fd) {
			h.notify_active = 0;
			raw_write(d->file, &h, sizeof h, 0);
		}
		give(d->lock);
	}
	NtClose(d->spaces);
	NtClose(d->items);
	NtClose(d->lock);
	memset(d, 0, sizeof *d);
}

int mq_close(mqd_t mqdes)
{
	if (!get_desc(mqdes)) return -1;
	return close(mqdes);
}

int mq_unlink(const char *name)
{
	char *path = mq_path(name), nsname[96];
	unsigned long long hash;
	HANDLE ns = 0;
	int result, saved;
	if (!path) return -1;
	if (ensure_dir(path) < 0) { free(path); return -1; }
	hash = path_hash(path);
	snprintf(nsname, sizeof nsname, "\\BaseNamedObjects\\ntlibc.mq.name.%08x%08x",
	         (unsigned)(hash >> 32), (unsigned)hash);
	if (create_sem(nsname, 1, 1, &ns) < 0 || take(ns) < 0) {
		saved = errno; if (ns) NtClose(ns); free(path); errno = saved; return -1;
	}
	result = unlink(path); saved = errno;
	give(ns); NtClose(ns); free(path); errno = saved;
	return result;
}
