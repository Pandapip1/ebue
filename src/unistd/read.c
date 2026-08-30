/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>
#include "libc.h"
#include "plat_fd.h"

ssize_t read(int fd, void *buf, size_t count)
{
	struct __fd *f = __fd_get(fd);

	if (!f) return -1;
	/* A socket goes through IOCTL_AFD_RECV (src/socket/sendrecv.c), not
	 * a plain read -- test/networking-audit.md sec 2 flags plain
	 * NtReadFile/NtWriteFile against a socket handle as unverified, and
	 * recv()/send() already have this fd's dispatch and error mapping,
	 * so read()/write() just forward to them rather than duplicating
	 * either. */
	if (f->type == __FD_SOCKET) return recv(fd, buf, count, 0);
	if ((f->flags & O_ACCMODE) == O_WRONLY) { errno = EBADF; return -1; }
	if (f->type == __FD_DIR) { errno = EISDIR; return -1; }
	if (count > 0x7fffffff) count = 0x7fffffff;
	if (!count) return 0;

	return __plat_read(f->h, buf, count);
}

ssize_t pread(int fd, void *buf, size_t count, off_t off)
{
	struct __fd *f = __fd_get(fd);
	long long saved;
	ssize_t r;

	if (!f) return -1;
	if ((f->flags & O_ACCMODE) == O_WRONLY) { errno = EBADF; return -1; }
	if (f->type != __FD_FILE) { errno = ESPIPE; return -1; }
	if (off < 0) { errno = EINVAL; return -1; }
	if (count > 0x7fffffff) count = 0x7fffffff;
	/* NT moves a synchronous handle's position to the end of a positioned
	 * transfer; POSIX says it must not move.  See src/internal/fdpos.c. */
	if (__fd_pos_save(f->h, &saved) < 0) return -1;
	r = __plat_pread(f->h, buf, count, off);
	__fd_pos_restore(f->h, saved);
	return r;
}
