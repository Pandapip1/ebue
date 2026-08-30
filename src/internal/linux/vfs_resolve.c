/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __vfs_resolve_at()/__vfs_open_dir() for Linux -- see src/internal/
 * nt/vfs_resolve.c's own banner for why this split exists and why
 * this half is a few lines rather than a port of that one's NT-
 * specific NtQueryAttributesFile-based probing: the overlay that file
 * implements exists to compensate for NT having no real filesystem
 * rooted at / at all (so no real /dev, no real /dev/null) -- a real
 * Linux process already has all of that, natively, for real, so the
 * fallback this whole subsystem exists to provide is simply never
 * needed here. Every path is native; __vfs_resolve_at() always
 * returns __VFS_NONE and __vfs_open_dir() is consequently never
 * reached at all (nothing on this platform ever gets back __VFS_ROOT/
 * __VFS_DEV to ask it for a synthetic directory handle in the first
 * place -- its own two real callers, src/fcntl/nt/plat_fcntl.c and
 * src/stat/nt/plat_stat.c, are themselves NT-only), kept here only so
 * the portable src/internal/libc.h declaration has something to link
 * against on every platform.
 */
#include <errno.h>
#include "libc.h"

int __vfs_resolve_at(int dirfd, const char *path)
{
	(void)dirfd;
	if (!path) { errno = EFAULT; return -1; }
	if (!*path) { errno = ENOENT; return -1; }
	return __VFS_NONE;
}

int __vfs_open_dir(int kind, int cloexec, HANDLE *out)
{
	(void)kind; (void)cloexec; (void)out;
	errno = ENOTDIR;
	return -1;
}
