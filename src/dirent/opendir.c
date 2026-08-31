/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * opendir/fdopendir: ask open() for an O_DIRECTORY descriptor and wrap it
 * in a DIR.  Going through open() is significant for the fixed POSIX
 * namespace: its directories have event handles only as lifetime and
 * inheritance carriers, and readdir() enumerates them without asking NT.
 *
 * The handle goes through the fd table like any other, which is what
 * makes dirfd() trivial and fdopendir() nearly free: fdopendir() does not
 * duplicate its argument, it just starts using the fd that is already
 * there, exactly as glibc's does, so the caller must not touch that fd
 * itself afterward and closedir() closes it.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "dirent_internal.h"

static DIR *alloc_dir(int fd)
{
	DIR *dp = __malloc(sizeof *dp);
	if (!dp) { errno = ENOMEM; return 0; }
	memset(dp, 0, sizeof *dp);
	dp->buf = __malloc(__DIRBUF_SIZE);
	if (!dp->buf) { __free(dp); errno = ENOMEM; return 0; }
	dp->fd = fd;
	dp->restart = 1;
	return dp;
}

DIR *fdopendir(int fd)
{
	struct __fd *f = __fd_get(fd);
	if (!f) return 0;
	if (f->type != __FD_DIR) { errno = ENOTDIR; return 0; }
	return alloc_dir(fd);
}

DIR *opendir(const char *path)
{
	int fd;
	DIR *dp;

	fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) return 0;

	dp = alloc_dir(fd);
	if (!dp) {
		int saved = errno;
		(void)close(fd);
		errno = saved;
		return 0;
	}
	return dp;
}

// NOLINTEND(misc-include-cleaner)
