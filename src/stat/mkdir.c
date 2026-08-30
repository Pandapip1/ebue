/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"
#include "plat_stat.h"

int mkdirat(int dirfd, const char *path, mode_t mode)
{
	struct __ntpath np;
	unsigned char mode_ea[32];
	unsigned ea_len;
	int r;
	int vfs = __vfs_resolve_at(dirfd, path);
	if (vfs < 0) return -1;
	if (vfs & __VFS_NATIVE) vfs = __VFS_NONE;
	if (vfs != __VFS_NONE) {
		errno = vfs == __VFS_MISSING ? EROFS : EEXIST;
		return -1;
	}

	mode = mode & ~__umask_get() & 07777;
	ea_len = __lxmod_create_buffer(mode_ea, S_IFDIR | mode);

	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE) < 0) return -1;
	r = __plat_mkdir(&np, mode_ea, ea_len);
	__ntpath_free(&np);
	return r;
}

int mkdir(const char *path, mode_t mode) { return mkdirat(AT_FDCWD, path, mode); }
