/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "libc.h"
#include "plat_unistd.h"

int chdir(const char *path)
{
	int vfs, kind, native;

	if (!path || !*path) { errno = ENOENT; return -1; }
	vfs = __vfs_resolve_at(AT_FDCWD, path);
	if (vfs < 0) return -1;
	native = (vfs & __VFS_NATIVE) != 0;
	kind = __VFS_KIND(vfs);
	if (kind == __VFS_MISSING) { errno = ENOENT; return -1; }
	if (kind != __VFS_NONE && kind != __VFS_ROOT && kind != __VFS_DEV) {
		errno = ENOTDIR;
		return -1;
	}
	/* Both virtual directories use the native drive root only as the
	 * process-parameter carrier; pathname dispatch uses vfs above. */
	if (kind != __VFS_NONE && !native) path = "/";
	/* chdir.html ERRORS, shall fail: "[ENAMETOOLONG] The length of a
	 * component of a pathname is longer than {NAME_MAX}."  chdir does
	 * not go through src/internal/path.c's builder -- the backend
	 * hand-builds its own UNICODE_STRING for RtlSetCurrentDirectory_U --
	 * so it has to ask for itself, or it would be the one path-taking
	 * interface in the library without the check.  Distinct from the
	 * whole-path bound __plat_chdir() applies to its own UNICODE_STRING;
	 * see __name_too_long()'s banner. */
	if (__name_too_long(path)) { errno = ENAMETOOLONG; return -1; }
	if (__plat_chdir(path) < 0) return -1;
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
