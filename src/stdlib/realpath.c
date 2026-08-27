/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* realpath: open the file and ask the kernel what it is called.  A path
 * that does not exist cannot be canonicalised that way, so it is an
 * ENOENT like POSIX says. */
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "libc.h"

char *realpath(const char *__restrict path, char *__restrict resolved)
{
	int fd, saved;
	char *p, *q;
	size_t len;
	int vfs;

	if (!path) { errno = EINVAL; return 0; }
	if (!*path) { errno = ENOENT; return 0; }
	vfs = __vfs_resolve_at(AT_FDCWD, path);
	if (vfs < 0) return 0;
	if (vfs & __VFS_NATIVE) vfs = __VFS_NONE;
	if (vfs == __VFS_MISSING) { errno = ENOENT; return 0; }
	if (vfs != __VFS_NONE) {
		static const char *const names[] = { 0, "/", "/dev", "/dev/console", "/dev/null", "/dev/tty" };
		const char *name = names[vfs];
		len = strlen(name);
		if (!resolved) {
			resolved = malloc(len + 1);
			if (!resolved) return 0;
		}
		memcpy(resolved, name, len + 1);
		return resolved;
	}
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		/* a directory might refuse O_RDONLY; try it as one */
		if (errno == EISDIR || errno == EACCES) fd = open(path, O_RDONLY | O_DIRECTORY);
		if (fd < 0) return 0;
	}
	p = __handle_path(__fd_handle(fd));
	saved = errno;
	close(fd);
	errno = saved;
	if (!p) return 0;
	for (q = p; *q; q++) if (*q == '\\') *q = '/';
	len = strlen(p);
	if (len > 3 && p[len-1] == '/') p[--len] = 0;
	if (!resolved) return p;
	if (len + 1 > PATH_MAX) { free(p); errno = ENAMETOOLONG; return 0; }
	memcpy(resolved, p, len + 1);
	free(p);
	return resolved;
}
