/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * open: the POSIX flags turned into NtCreateFile's.
 *
 * Every file is opened synchronous (FILE_SYNCHRONOUS_IO_NONALERT) so that
 * the kernel keeps the file position and read/write need not; and with
 * all three share modes, which is what Unix semantics demand and what
 * lets one program delete a file another has open.  Handles are made
 * inheritable unless O_CLOEXEC says otherwise, because fork needs them
 * copied and exec passes them on.
 *
 * A newly created file receives WSL's $LXMOD NTFS extended attribute;
 * FILE_ATTRIBUTE_READONLY also mirrors the aggregate write bits for
 * ordinary Windows programs.
 */
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include "libc.h"
#include "plat_fd.h"
#include "plat_fcntl.h"

int __open_handle(int dirfd, const char *path, int flags, unsigned mode,
                  __plat_handle_t *out, int *typeout, int *vfsout, int *vfsnativeout)
{
	struct __ntpath np;
	int type;
	unsigned char mode_ea[32];
	void *ea = 0;
	unsigned ea_len = 0;
	int vfs, native;

	*vfsout = __VFS_NONE;
	*vfsnativeout = 0;
	if (!path) { errno = EFAULT; return -1; }

	/* /dev/stdin, /dev/stdout, /dev/stderr and /dev/fd/N are the fd table. */
	if (!strncmp(path, "/dev/", 5)) {
		int fd = -1;
		if (!strcmp(path, "/dev/stdin")) fd = 0;
		else if (!strcmp(path, "/dev/stdout")) fd = 1;
		else if (!strcmp(path, "/dev/stderr")) fd = 2;
		else if (!strncmp(path, "/dev/fd/", 8)) { fd = 0; { const char *q = path + 8; while (*q >= '0' && *q <= '9') fd = fd * 10 + *q++ - '0'; if (*q) fd = -1; } }
		if (fd >= 0) {
			struct __fd *f = __fd_get(fd);
			if (!f) return -1;
			if (__plat_dup(f->h, !(flags & O_CLOEXEC), out) < 0) return -1;
			*typeout = f->type;
			*vfsout = f->vfs; *vfsnativeout = f->vfs_native;
			return 0;
		}
	}

	vfs = __vfs_resolve_at(dirfd, path);
	if (vfs < 0) return -1;
	native = (vfs & __VFS_NATIVE) != 0;
	if (native) {
		*vfsout = __VFS_KIND(vfs);
		*vfsnativeout = 1;
		vfs = __VFS_NONE;
	}
	if (vfs == __VFS_MISSING) {
		errno = flags & O_CREAT ? EROFS : ENOENT;
		return -1;
	}
	if (vfs == __VFS_ROOT || vfs == __VFS_DEV) {
		if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) { errno = EEXIST; return -1; }
		if ((flags & O_ACCMODE) != O_RDONLY || (flags & O_TRUNC)) { errno = EISDIR; return -1; }
		if (__vfs_open_dir(vfs, flags & O_CLOEXEC, out) < 0) return -1;
		*typeout = __FD_DIR;
		*vfsout = vfs;
		return 0;
	}
	if (vfs == __VFS_CONSOLE || vfs == __VFS_NULL || vfs == __VFS_TTY) {
		if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) { errno = EEXIST; return -1; }
		if (flags & O_DIRECTORY) { errno = ENOTDIR; return -1; }
		path = vfs == __VFS_NULL ? "NUL" : "CON";
		dirfd = AT_FDCWD;
		*vfsout = vfs;
	}

	if (__ntpath_at(dirfd, path, &np, OBJ_CASE_INSENSITIVE | (flags & O_CLOEXEC ? 0 : OBJ_INHERIT)) < 0)
		return -1;

	/* open.html DESCRIPTION: mode is ANDed with the complement of umask.
	 * The $LXMOD extended-attribute buffer is this library's own POSIX-
	 * mode-persistence strategy (see src/stat/lxmod.c), built here and
	 * handed to the backend rather than built there, exactly like
	 * mman.c's reservation table stays in the front door: it is not an
	 * NT interpretation step, it is this library's own choice of how to
	 * remember a POSIX mode at all. */
	if (flags & O_CREAT) {
		mode = mode & ~__umask_get() & 07777;
		ea_len = __lxmod_create_buffer(mode_ea, S_IFREG | mode);
		ea = mode_ea;
	}

	{
		int r = __plat_create_file(&np, flags, mode, ea, ea_len, out, &type);
		__ntpath_free(&np);
		if (r < 0) return -1;
	}
	*typeout = type;
	return 0;
}

int openat(int dirfd, const char *path, int flags, ...)
{
	mode_t mode = 0;
	__plat_handle_t h;
	int type, fd, vfs, vfs_native;

	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	if (__open_handle(dirfd, path, flags, mode, &h, &type, &vfs, &vfs_native) < 0) return -1;
	fd = __fd_install(h, flags & (O_APPEND | O_NONBLOCK | O_CLOEXEC | O_ACCMODE), type);
	if (fd < 0) { __plat_close(h); return -1; }
	__fds[fd].vfs = (unsigned char)vfs;
	__fds[fd].vfs_native = (unsigned char)vfs_native;
	return fd;
}

int open(const char *path, int flags, ...)
{
	mode_t mode = 0;
	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	return openat(AT_FDCWD, path, flags, mode);
}

int creat(const char *path, mode_t mode)
{
	return open(path, O_CREAT | O_WRONLY | O_TRUNC, mode);
}
