/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"
#include "plat_unistd.h"

int ftruncate(int fd, off_t len) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct __fd *f = __fd_get(fd);
	if (!f) return -1;
	if (len < 0) { errno = EINVAL; return -1; }
	/* ftruncate.html: "The fildes argument is not a file descriptor open
	 * for writing" is a mandatory [EINVAL].  Letting NT reject the
	 * FileEndOfFileInformation request mapped STATUS_ACCESS_DENIED to EBADF
	 * instead, which is observably the wrong clause for an otherwise-valid
	 * O_RDONLY descriptor. */
	if ((f->flags & O_ACCMODE) == O_RDONLY) { errno = EINVAL; return -1; }
	/* RLIMIT_FSIZE (src/misc/resource.c).  ftruncate cannot partially
	 * succeed, so it fails outright rather than clamping the way write()
	 * does -- measured against Linux/glibc, where ftruncate above the
	 * limit is [EFBIG] and shrinking to below it is always allowed. */
	if (__fsize_allow((long long)len) < 0) return -1;
	return __plat_ftruncate(f->h, len);
}

int truncate(const char *path, off_t len)
{
	int fd = open(path, O_WRONLY);
	int r;
	if (fd < 0) return -1;
	r = ftruncate(fd, len);
	if (r < 0) {
		int saved = errno;
		(void)close(fd);
		errno = saved;
	} else if (close(fd) < 0) {
		return -1;
	}
	return r;
}
