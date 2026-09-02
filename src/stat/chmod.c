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

/* mkfifo()/mknod() and their *at() siblings all reduce to one call:
 * Linux's own mknodat(2) already IS "mknod, optionally relative to a
 * dirfd", and mkfifo() is simply that with S_IFIFO forced into the type
 * bits and no device -- so mkfifoat() ORs it in and every one of the
 * four forwards straight to __plat_mknod() (src/internal/plat_stat.h).
 * NT has no filesystem node type any of this maps onto -- no POSIX FIFO
 * semantics mapped onto its own named-pipe object, no device-node
 * concept on NTFS at all -- so its own __plat_mknod() (src/stat/nt/
 * plat_stat.c) stays the unconditional ENOSYS-for-FIFO/EPERM-for-
 * anything-else stub every one of these four calls always was before
 * this indirection existed; Linux's own (src/stat/linux/plat_stat.c)
 * creates the node for real. mode is passed through whole (S_IF* type
 * bits and permission bits together) rather than masked here: which
 * bits are meaningful, and which of them the real kernel's own umask
 * applies to, is each backend's own business, exactly like
 * __plat_mkdir() above already leaves mode unmasked for the same
 * reason. */
int mkfifo(const char *p, mode_t m) { return mkfifoat(AT_FDCWD, p, m); }
int mkfifoat(int d, const char *p, mode_t m) { return __plat_mknod(d, p, (m & 07777) | S_IFIFO, 0); }
int mknod(const char *p, mode_t m, dev_t dv) { return mknodat(AT_FDCWD, p, m, dv); } // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
int mknodat(int d, const char *p, mode_t m, dev_t dv) { return __plat_mknod(d, p, m, dv); } // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
