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
	free(path);
	return r;
}

int fchmodat(int dirfd, const char *path, mode_t mode, int flags)
{
	/* fchmodat.html ERRORS: "[EINVAL] The value of the flag argument is
	 * invalid."  AT_SYMLINK_NOFOLLOW is the only flag this page defines,
	 * so every other bit is invalid.  It is a *may fail* error, so
	 * ignoring the bits was conforming -- but silently succeeding on a
	 * flag the caller believes it asked for is the worse of the two legal
	 * answers, and glibc reports EINVAL here (measured: fchmodat(AT_FDCWD,
	 * path, 0644, 0x4000) -> -1/EINVAL, while AT_SYMLINK_NOFOLLOW and 0
	 * both succeed).  Checked here, in the portable front door, rather
	 * than inside __plat_chmodat(): it needs no path resolution and is
	 * not platform-specific, so failing fast here costs no path
	 * conversion on any backend and cannot be masked by a path error. */
	if (flags & ~AT_SYMLINK_NOFOLLOW) { errno = EINVAL; return -1; }
	return __plat_chmodat(dirfd, path, flags, mode);
}

int chmod(const char *path, mode_t mode) { return fchmodat(AT_FDCWD, path, mode, 0); }

static mode_t umask_value = 022;
mode_t umask(mode_t m) { mode_t o = umask_value; umask_value = m & 0777; return o; }
unsigned __umask_get(void) { return umask_value; }

int mkfifo(const char *p, mode_t m) { (void)p; (void)m; errno = ENOSYS; return -1; }
int mkfifoat(int d, const char *p, mode_t m) { (void)d; (void)p; (void)m; errno = ENOSYS; return -1; }
int mknod(const char *p, mode_t m, dev_t dv) { (void)p; (void)m; (void)dv; errno = EPERM; return -1; } // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
int mknodat(int d, const char *p, mode_t m, dev_t dv) { (void)d; (void)p; (void)m; (void)dv; errno = EPERM; return -1; } // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
