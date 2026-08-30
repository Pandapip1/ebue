/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * stat, from the information NT keeps.
 *
 * Permission and special bits come from WSL's $LXMOD NTFS extended attribute
 * when it exists.  It is a four-byte mode value and gives files created by
 * ntlibc a persistent POSIX mode without editing a Windows DACL.  ntlibc does
 * not create $LXUID or $LXGID: those are literal IDs in a WSL distribution
 * and cannot be derived from ntlibc's Windows process identity.
 *
 * A file with no $LXMOD keeps a compatibility default for pre-existing
 * Windows files: directories are 0755, ordinary files 0644, and an actual
 * executable PE32/PE32+ image receives 0111 regardless of its suffix.
 * FILE_ATTRIBUTE_READONLY removes 0222.  A suffix alone never grants execute
 * permission, and explicit $LXMOD metadata always wins.
 *
 * Pipes, consoles, character devices and the "couldn't classify it"
 * fallback get a synthetic st_dev/st_ino instead: stat.html's DESCRIPTION
 * requires "[st_ino] together with [st_dev] uniquely identify the file
 * within the system" (see also <sys/stat.h>'s own text to that effect),
 * and the universal same-file idiom (`a.st_dev==b.st_dev &&
 * a.st_ino==b.st_ino`) depends on it.  The guts of stat()/fstat(),
 * including how that synthetic identity is derived, now live in
 * __plat_fstat()/__plat_fstatat() (src/stat/nt/plat_stat.c) --
 * everything NT-specific about turning a handle into a struct stat is
 * there, not here.
 */
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "libc.h"
#include "plat_stat.h"

int fstat(int fd, struct stat *st)
{
	struct __fd *f = __fd_get(fd);
	int result;
	if (!f) return -1;
	if (f->vfs && !f->vfs_native) return __vfs_stat(f->vfs, st);
	result = __plat_fstat(f->h, f->type, st);
	/* Wine does not retain the $LXMOD EA supplied to NtCreateFile.  A
	 * shm descriptor carries the mode read from its private namespace
	 * sidecar so fstat() can report the same persistent POSIX metadata. */
	if (result == 0 && f->shm_mode_valid)
		st->st_mode = (st->st_mode & S_IFMT) | (f->shm_mode & 07777);
	return result;
}

int fstatat(int dirfd, const char *path, struct stat *st, int flags)
{
	if (!path) { errno = EFAULT; return -1; }
	/* /dev/stdin, /dev/stdout, /dev/stderr are the fd table -- genuinely
	 * portable POSIX-shaped logic (every backend's fd table works the
	 * same way), so this stays in the front door rather than moving into
	 * __plat_fstatat() alongside the NT-specific VFS-overlay/path-
	 * resolution machinery below it -- see src/fcntl/open.c's own /dev/
	 * std* special case for the precedent this mirrors. */
	if (!strncmp(path, "/dev/", 5)) {
		int fd = -1;
		if (!strcmp(path, "/dev/stdin")) fd = 0;
		else if (!strcmp(path, "/dev/stdout")) fd = 1;
		else if (!strcmp(path, "/dev/stderr")) fd = 2;
		if (fd >= 0) return fstat(fd, st);
	}
	return __plat_fstatat(dirfd, path, flags, st);
}

int stat(const char *path, struct stat *st) { return fstatat(AT_FDCWD, path, st, 0); }
int lstat(const char *path, struct stat *st) { return fstatat(AT_FDCWD, path, st, AT_SYMLINK_NOFOLLOW); }
