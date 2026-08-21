/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "libc.h"

int faccessat(int dirfd, const char *path, int mode, int flags)
{
	struct stat st;
	(void)flags;
	if (fstatat(dirfd, path, &st, 0) < 0) return -1;
	if ((mode & W_OK) && !(st.st_mode & 0222)) { errno = EACCES; return -1; }
	if ((mode & X_OK) && !(st.st_mode & 0111)) { errno = EACCES; return -1; }
	return 0;
}

int access(const char *path, int mode)
{
	return faccessat(AT_FDCWD, path, mode, 0);
}
