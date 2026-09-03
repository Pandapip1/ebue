/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>
#include "libc.h"
#include "plat_fd.h"

ssize_t write(int fd, const void *buf withtok(readable_span(count)),
	size_t count)
{
	struct __fd *f = __fd_get(fd);
	int append;

	if (!f) return -1;
	/* See read.c's matching branch: a socket goes through send(), not a
	 * plain write. */
	if (f->type == __FD_SOCKET) return send(fd, buf, count, 0);
	if ((f->flags & O_ACCMODE) == O_RDONLY) { errno = EBADF; return -1; }
	if (f->type == __FD_DIR) { errno = EISDIR; return -1; }
	if (count > 0x7fffffff) count = 0x7fffffff;
	if (!count) return 0;

	/* RLIMIT_FSIZE: clamp to the process file-size limit, or raise
	 * SIGXFSZ and fail with [EFBIG] when not one byte may be written
	 * (src/misc/resource.c's __fsize_exceeded(), which does both in the
	 * order setrlimit.html needs and may not return at all).  Only
	 * regular files have a size this can be about, and the predicate
	 * short-circuits when no limit is set, so an unlimited process pays
	 * nothing. */
	if (f->type == __FD_FILE && __fsize_limited()) {
		long long room = __fsize_clamp(f->h, (f->flags & O_APPEND) != 0, count);
		if (room < 0) return -1;
		count = (size_t)room;
	}

	append = (f->flags & O_APPEND) && f->type == __FD_FILE;
	return __plat_write(f->h, buf, count, append);
}

ssize_t pwrite(int fd, const void *buf withtok(readable_span(count)),
	size_t count, off_t off)
{
	struct __fd *f = __fd_get(fd);
	long long saved;
	ssize_t r;

	if (!f) return -1;
	if ((f->flags & O_ACCMODE) == O_RDONLY) { errno = EBADF; return -1; }
	if (f->type != __FD_FILE) { errno = ESPIPE; return -1; }
	if (off < 0) { errno = EINVAL; return -1; }
	if (count > 0x7fffffff) count = 0x7fffffff;
	/* write.html's EFBIG offset maximum, measured against the caller's
	 * offset arg (pwrite needs no position query, unlike write()). A
	 * request straddling the maximum is a short write, not an error;
	 * both arms are unsigned so off + count can't overflow a signed
	 * off_t. This is the file's own limit, not the process's, so unlike
	 * below it raises no SIGXFSZ. */
	if (count && off >= __OFF_MAX) { errno = EFBIG; return -1; }
	if ((unsigned long long)off + count > (unsigned long long)__OFF_MAX)
		count = (size_t)(__OFF_MAX - off);
	/* RLIMIT_FSIZE, measured against the caller's offset since pwrite
	 * writes where it's told; this IS the process limit, so it goes
	 * through __fsize_exceeded() and generates SIGXFSZ. */
	if (__fsize_limited()) {
		long long room = __fsize_room_at(off);
		if (room <= 0) return __fsize_exceeded();
		if ((long long)count > room) count = (size_t)room;
	}
	/* NT moves a synchronous handle's position to the end of a positioned
	 * transfer; POSIX says it must not move.  See src/internal/fdpos.c. */
	if (__fd_pos_save(f->h, &saved) < 0) return -1;
	r = __plat_pwrite(f->h, buf, count, off);
	__fd_pos_restore(f->h, saved);
	return r;
}
