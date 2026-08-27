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

int __open_handle(int dirfd, const char *path, int flags, unsigned mode,
                  HANDLE *out, int *typeout, int *vfsout, int *vfsnativeout)
{
	struct __ntpath np;
	IO_STATUS_BLOCK io;
	ACCESS_MASK access;
	ULONG disposition, options, attrs;
	NTSTATUS st;
	HANDLE h;
	int type;
	unsigned char mode_ea[32];
	void *ea = 0;
	ULONG ea_len = 0;
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
			st = NtDuplicateObject(NtCurrentProcess(), f->h, NtCurrentProcess(), &h, 0,
			                       flags & O_CLOEXEC ? 0 : OBJ_INHERIT, DUPLICATE_SAME_ACCESS);
			if (!NT_SUCCESS(st)) { __set_errno_status(st); return -1; }
			*out = h; *typeout = f->type;
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

	access = SYNCHRONIZE | FILE_READ_ATTRIBUTES | FILE_READ_EA;
	switch (flags & O_ACCMODE) {
	case O_RDONLY: access |= FILE_GENERIC_READ; break;
	case O_WRONLY: access |= FILE_GENERIC_WRITE; break;
	case O_RDWR:   access |= FILE_GENERIC_READ | FILE_GENERIC_WRITE; break; // NOLINT(misc-redundant-expression) -- both masks include SYNCHRONIZE, harmless ORed twice
	/* The fourth access mode, 03, is O_EXEC and O_SEARCH -- equal
	 * values, as fcntl.h.html permits.  Refused, not served: each asks
	 * for a handle that can do LESS than a read handle (execute-only on
	 * a file, traverse-only on a directory), so quietly widening either
	 * to O_RDONLY would grant more access than the caller asked for and
	 * return success -- the one way a request to be restricted must not
	 * fail.  [EINVAL] "The value of the oflag argument is not valid" is
	 * also what this arm answered before those two names existed, so
	 * naming them changed no behaviour.  Serving them for real means
	 * FILE_EXECUTE / FILE_TRAVERSE access masks and the fd-table and
	 * read()/write() checks that go with a mode neither reads nor
	 * writes; that is not done here. */
	default: __ntpath_free(&np); errno = EINVAL; return -1;
	}
	if (flags & O_APPEND) access = (access & ~FILE_WRITE_DATA) | FILE_APPEND_DATA;
	if (flags & O_TRUNC) access |= FILE_WRITE_DATA;   /* overwrite needs it */
	if (flags & O_PATH) access = SYNCHRONIZE | FILE_READ_ATTRIBUTES | FILE_READ_EA;

	switch (flags & (O_CREAT | O_EXCL | O_TRUNC)) {
	case 0:
	case O_EXCL:                  disposition = FILE_OPEN; break;
	case O_CREAT:                 disposition = FILE_OPEN_IF; break;
	case O_CREAT | O_EXCL:
	case O_CREAT | O_EXCL | O_TRUNC: disposition = FILE_CREATE; break;
	case O_TRUNC:
	case O_TRUNC | O_EXCL:        disposition = FILE_OVERWRITE; break;
	case O_CREAT | O_TRUNC:       disposition = FILE_OVERWRITE_IF; break;
	default: disposition = FILE_OPEN; break;
	}

	options = FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT;
	if (flags & O_DIRECTORY) options |= FILE_DIRECTORY_FILE;
	else if (disposition != FILE_OPEN && disposition != FILE_OPEN_IF) options |= FILE_NON_DIRECTORY_FILE;
	if (flags & O_NOFOLLOW) options |= FILE_OPEN_REPARSE_POINT;
	if (flags & (O_SYNC | O_DSYNC)) options |= FILE_WRITE_THROUGH;
	if (flags & O_DIRECT) options |= FILE_NO_INTERMEDIATE_BUFFERING;

	attrs = FILE_ATTRIBUTE_NORMAL;
	/* open.html DESCRIPTION: mode is ANDed with the complement of umask.
	 * NtCreateFile only applies its EA buffer when it creates the object,
	 * so O_CREAT on an existing file cannot overwrite that file's mode. */
	if (flags & O_CREAT) {
		mode = mode & ~__umask_get() & 07777;
		if (!(mode & 0222)) attrs = FILE_ATTRIBUTE_READONLY;
		ea_len = __lxmod_create_buffer(mode_ea, S_IFREG | mode);
		ea = mode_ea;
	}

	st = NtCreateFile(&h, access, &np.oa, &io, 0, attrs, FILE_SHARE_VALID_FLAGS,
	                  disposition, options, ea, ea_len);

	/* A directory opened without O_DIRECTORY for reading: allowed by
	 * POSIX (reads then fail with EISDIR); NT refuses FILE_NON_DIRECTORY
	 * only when we asked for it, and refuses data access on directories
	 * with STATUS_FILE_IS_A_DIRECTORY, so retry as a directory. */
	if (st == STATUS_FILE_IS_A_DIRECTORY && (flags & O_ACCMODE) == O_RDONLY && !(flags & O_CREAT)) {
		options |= FILE_DIRECTORY_FILE;
		access = SYNCHRONIZE | FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES |
		         FILE_READ_EA | FILE_TRAVERSE;
		st = NtCreateFile(&h, access, &np.oa, &io, 0, attrs, FILE_SHARE_VALID_FLAGS, FILE_OPEN, options, 0, 0);
	}
	/* Writing to a directory is EISDIR, not EACCES. */
	if (st == STATUS_FILE_IS_A_DIRECTORY) { __ntpath_free(&np); errno = EISDIR; return -1; }
	__ntpath_free(&np);
	if (!NT_SUCCESS(st)) {
		/* FILE_CREATE on an existing directory, etc. */
		if (st == STATUS_OBJECT_NAME_COLLISION) errno = EEXIST;
		else __set_errno_status(st);
		return -1;
	}

	type = (options & FILE_DIRECTORY_FILE) ? __FD_DIR : 0;
	*out = h;
	*typeout = type;
	return 0;
}

int openat(int dirfd, const char *path, int flags, ...)
{
	mode_t mode = 0;
	HANDLE h;
	int type, fd, vfs, vfs_native;

	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}
	if (__open_handle(dirfd, path, flags, mode, &h, &type, &vfs, &vfs_native) < 0) return -1;
	fd = __fd_install(h, flags & (O_APPEND | O_NONBLOCK | O_CLOEXEC | O_ACCMODE), type);
	if (fd < 0) { NtClose(h); return -1; }
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
