/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"
#include "plat_stat.h"

int mkdirat(int dirfd, const char *path, mode_t mode)
{
	return __plat_mkdir(dirfd, path, mode);
}

int mkdir(const char *path, mode_t mode) { return mkdirat(AT_FDCWD, path, mode); }
