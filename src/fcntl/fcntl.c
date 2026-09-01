/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)

#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "ownership_stubs.h"
#include "plat_fd.h"
#include "plat_fcntl.h"

struct record_lock_state {
	__plat_handle_t h;
	long long off;
	long long len;
	pid_t owner;
	unsigned char held;
};

/* NtUnlockFile on a range which was never locked wedges some Wine
 * versions instead of returning STATUS_RANGE_NOT_LOCKED.  Remember the
 * single range placed through each descriptor so an ordinary redundant
 * F_UNLCK remains the harmless success POSIX requires.  The owner check is
 * essential after fork(): the memory is copied, but record locks are not. */
static struct record_lock_state record_locks[FD_MAX];

static int record_lock_range(struct __fd *f, const struct flock *l,
			     long long *off, long long *len) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	long long base, start, length;

	switch (l->l_whence) {
	case SEEK_SET:
		base = 0;
		break;
	case SEEK_CUR:
		base = __plat_seek_query(f->h, 0);
		if (base < 0) return -1;
		break;
	case SEEK_END:
		base = __plat_seek_query(f->h, 1);
		if (base < 0) return -1;
		break;
	default:
		errno = EINVAL;
		return -1;
	}
	if (l->l_start > 0 && base > __OFF_MAX - l->l_start) {
		errno = EOVERFLOW;
		return -1;
	}
	start = base + l->l_start;
	if (start < 0) { errno = EINVAL; return -1; }

	if (l->l_len < 0) {
		if (l->l_len == -__OFF_MAX - 1) { errno = EOVERFLOW; return -1; }
		if (start < -l->l_len) { errno = EINVAL; return -1; }
		start += l->l_len;
		length = -l->l_len;
	} else if (l->l_len == 0) {
		/* NT has no to-EOF sentinel.  Its signed 64-bit maximum covers
		 * every byte representable by this libc's off_t. */
		length = 0x7fffffffffffffffLL - start;
		if (!length) length = 1;
	} else {
		if (start > __OFF_MAX - l->l_len) { errno = EOVERFLOW; return -1; }
		length = l->l_len;
	}
	*off = start;
	*len = length;
	return 0;
}

static int record_lock(int fd, struct __fd *f, int cmd, struct flock *l)
{
	struct record_lock_state *held = &record_locks[fd];
	long long off = 0, len = 0;
	pid_t owner = getpid();
	int exclusive;

	if (!l || (l->l_type != F_RDLCK && l->l_type != F_WRLCK &&
	           l->l_type != F_UNLCK)) {
		errno = EINVAL;
		return -1;
	}
	if (record_lock_range(f, l, &off, &len) < 0) return -1;
	if (held->held && (held->h != f->h || held->owner != owner))
		held->held = 0;

	if (cmd == F_GETLK) {
		int conflicting;
		if (l->l_type == F_UNLCK) { errno = EINVAL; return -1; }
		if (held->held && off < held->off + held->len &&
		    held->off < off + len) {
			l->l_type = F_UNLCK;
			return 0;
		}
		exclusive = l->l_type == F_WRLCK;
		if (__plat_lock_probe(f->h, off, len, exclusive, &conflicting) < 0) return -1;
		if (conflicting) {
			/* NT does not expose the owning process for a byte-range lock. */
			l->l_pid = (pid_t)-1;
			return 0;
		}
		l->l_type = F_UNLCK;
		return 0;
	}

	if (l->l_type == F_UNLCK) {
		/* See record_locks' Wine note above. */
		if (!held->held || held->off != off || held->len != len) return 0;
		{
			int r = __plat_lock_clear(f->h, off, len);
			held->held = 0;
			if (r < 0) return -1;
		}
		return 0;
	}

	exclusive = l->l_type == F_WRLCK;
	if (__plat_lock_set(f->h, off, len, exclusive, cmd == F_SETLKW) < 0) return -1;
	held->h = f->h;
	held->owner = owner;
	held->off = off;
	held->len = len;
	held->held = 1;
	return 0;
}

int fcntl(int fd, int cmd, ...)
{
	struct __fd *f = __fd_get(fd);
	va_list ap;
	intptr_t arg;

	if (!f) return -1;
	va_start(ap, cmd);
	arg = va_arg(ap, intptr_t);
	va_end(ap);

	switch (cmd) {
	case F_DUPFD:
	case F_DUPFD_CLOEXEC: {
		int nfd;
		__plat_handle_t h;
		if (arg < 0 || arg >= FD_MAX) { errno = EINVAL; return -1; }
		nfd = __fd_alloc((int)arg);
		if (nfd < 0) return -1;
		if (__plat_dup(f->h, cmd != F_DUPFD_CLOEXEC, &h) < 0) return -1;
		__fd_install_at(nfd, h, (f->flags & ~O_CLOEXEC) | (cmd == F_DUPFD_CLOEXEC ? O_CLOEXEC : 0), f->type);
		__fds[nfd].pad = f->pad;
		__fds[nfd].shm_mode_valid = f->shm_mode_valid;
		__fds[nfd].shm_mode = f->shm_mode;
		__fds[nfd].vfs = f->vfs;
		__fds[nfd].vfs_native = f->vfs_native;
		__fds[nfd].vseen = f->vseen;
		__fds[nfd].vnext = f->vnext;
		__fds[nfd].peer_len = f->peer_len;
		__ownership_writable_span(__fds[nfd].peer,
			 sizeof __fds[nfd].peer);
		__ownership_readable_span(f->peer, sizeof f->peer);
		memcpy(__fds[nfd].peer, f->peer, sizeof f->peer);
		return nfd;
	}
	case F_GETFD:
		return f->flags & O_CLOEXEC ? FD_CLOEXEC : 0;
	case F_SETFD: {
		__plat_handle_t h;
		unsigned want = arg & FD_CLOEXEC ? O_CLOEXEC : 0;
		if ((f->flags & O_CLOEXEC) == want) return 0;
		/* Inheritability is a property of the handle; remake it. */
		if (__plat_dup(f->h, !want, &h) < 0) return -1;
		__plat_close(f->h);
		f->h = h;
		__mq_fd_replaced(fd, h);
		f->flags = (f->flags & ~O_CLOEXEC) | want;
		return 0;
	}
	case F_GETFL:
		/* Only these public, int-representable flag bits survive the mask. */
		return (int)(f->flags & (O_ACCMODE | O_APPEND | O_NONBLOCK));
	case F_SETFL:
		f->flags = (f->flags & ~(O_APPEND | O_NONBLOCK)) | (arg & (O_APPEND | O_NONBLOCK));
		return 0;
	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		return record_lock(fd, f, cmd, (struct flock *)arg);
	default:
		errno = EINVAL;
		return -1;
	}
}

// NOLINTEND(misc-include-cleaner)
