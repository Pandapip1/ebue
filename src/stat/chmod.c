/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

/* chmod persists the POSIX mode in WSL's $LXMOD NTFS extended attribute.
 * FILE_ATTRIBUTE_READONLY is kept in sync with the aggregate write bits so
 * ordinary Windows programs observe the closest native equivalent too. */
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include "libc.h"
#include "plat_stat.h"

int fchmod(int fd, mode_t mode) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct __fd *f = __fd_get(fd);
	char *path;
	int r, e;
	if (!f) return -1;
	if (f->vfs && !f->vfs_native) { errno = EROFS; return -1; }
	if (f->type != __FD_FILE && f->type != __FD_DIR) return 0;
	r = __plat_chmod(f->h, mode);
	if (r == 0 || errno != EACCES) return r;
	/* O_RDONLY handles do not carry FILE_WRITE_EA.  Reopen the same object
	 * by name with that right, as path-taking chmod does.  An unlinked file
	 * has no reopenable name and correctly retains the original failure. */
	e = errno;
	path = __handle_path(f->h);
	if (!path) { errno = e; return -1; }
	r = fchmodat(AT_FDCWD, path, mode, 0);
	__free(path);
	return r;
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
	/* AT_SYMLINK_NOFOLLOW is the only defined flag; any other bit is
	 * rejected rather than silently ignored, matching glibc. */
	if (flags & ~AT_SYMLINK_NOFOLLOW) { errno = EINVAL; return -1; }
	return __plat_chmodat(dirfd, path, flags, mode);
}

int chmod(const char *path, mode_t mode) { return fchmodat(AT_FDCWD, path, mode, 0); }

/* umask_value is pushed out to the real OS-level mask (where one
 * exists) via __plat_umask_apply() on every call, not just tracked in
 * userspace -- see that function's own comment (plat_stat.h) for why
 * that matters on Linux and why it is a deliberate no-op on NT. */
static mode_t umask_value = 022;
mode_t umask(mode_t m)
{
	mode_t o = umask_value;
	umask_value = m & 0777;
	__plat_umask_apply(umask_value);
	return o;
}
unsigned __umask_get(void) { return umask_value; }

/* mkfifo()/mknod() and their *at() siblings all forward to __plat_mknod().
 * NT has no device-node or FIFO filesystem type, so its __plat_mknod()
 * always returns ENOSYS/EPERM; Linux's creates the node for real. mode is
 * passed through unmasked, like mkdir.c's __plat_mkdir(), since which
 * bits are meaningful is each backend's own business. */
int mkfifo(const char *p, mode_t m) { return mkfifoat(AT_FDCWD, p, m); }
int mkfifoat(int d, const char *p, mode_t m) { return __plat_mknod(d, p, (m & 07777) | S_IFIFO, 0); }
int mknod(const char *p, mode_t m, dev_t dv) { return mknodat(AT_FDCWD, p, m, dv); } // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
int mknodat(int d, const char *p, mode_t m, dev_t dv) { return __plat_mknod(d, p, m, dv); } // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
