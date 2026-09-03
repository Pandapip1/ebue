/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include <errno.h>
#include "libc.h"
#include "plat_unistd.h"

int fsync(int fd)
{
	struct __fd *f = __fd_get(fd);
	if (!f) return -1;
	/* fsync.html: EINVAL if fildes "is bound to a special file which
	 * does not support synchronization" -- a pipe, socket, console or
	 * character device, none of which have buffered writeback to force.
	 * Reporting success instead would be indistinguishable from a real
	 * fsync() to the caller. */
	if (f->type != __FD_FILE) { errno = EINVAL; return -1; }
	return __plat_fsync(f->h);
}

int fdatasync(int fd) { return fsync(fd); }
void sync(void) {}
