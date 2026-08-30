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
	/* fsync.html ERRORS: "[EINVAL] fildes is bound to a special file
	 * which does not support synchronization." A pipe, socket,
	 * console, or character device is exactly that -- there is no
	 * buffered writeback to force for any of them, and reporting
	 * success anyway (this used to, for every non-regular-file type)
	 * cannot be told apart from a real fsync() by a caller, which is
	 * the same shape of problem this codebase declines elsewhere (see
	 * src/mman/mman.c's msync(), an HONEST no-op only where the
	 * postcondition genuinely holds vacuously -- an anonymous mapping
	 * has no object to flush, unlike a pipe, which POSIX gives an
	 * errno to decline with instead). */
	if (f->type != __FD_FILE) { errno = EINVAL; return -1; }
	return __plat_fsync(f->h);
}

int fdatasync(int fd) { return fsync(fd); }
void sync(void) {}
