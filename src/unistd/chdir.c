/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "libc.h"
#include "plat_unistd.h"

int chdir(const char *path)
{
	int vfs;

	if (!path || !*path) { errno = ENOENT; return -1; }
	/* Resolving `path` through the fixed POSIX namespace (kind/native
	 * checks, ENOTDIR, the "/" substitution for a non-native virtual
	 * directory) and the {NAME_MAX}-per-component check used to live
	 * here; both moved into __plat_chdir() (src/internal/plat_unistd.h)
	 * alongside the actual native chdir -- __vfs_resolve_at() is NT-only
	 * machinery no backend without NT's own POSIX-namespace overlay
	 * needs, exactly the same relocation open()'s own front door got
	 * (see src/fcntl/open.c). __vfs_cwd_set() below is NOT part of that
	 * move: it is this library's own process-wide cwd-kind bookkeeping
	 * (src/internal/vfs.c's static cwd_kind, read back by every future
	 * __vfs_resolve_at() call, AT_FDCWD-relative or not), not a
	 * resolution step, so it stays genuinely portable front-door state --
	 * a backend with no overlay concept at all reports __VFS_NONE via
	 * *vfsout, and __vfs_cwd_set(__VFS_NONE) is exactly the harmless
	 * no-op __vfs_resolve_at()'s own `else if (cwd_kind)` branch already
	 * treats it as. */
	if (__plat_chdir(path, &vfs) < 0) return -1;
	__vfs_cwd_set(vfs);
	return 0;
}

int fchdir(int fd)
{
	struct __fd *f = __fd_get(fd);
	char *p;
	int r;
	if (!f) return -1;
	if (f->type != __FD_DIR) { errno = ENOTDIR; return -1; }
	if (!f->vfs_native && f->vfs == __VFS_ROOT) return chdir("/");
	if (!f->vfs_native && f->vfs == __VFS_DEV) return chdir("/dev");
	p = __handle_path(f->h);
	if (!p) return -1;
	r = chdir(p);
	__free(p);
	return r;
}

// NOLINTEND(misc-include-cleaner)
