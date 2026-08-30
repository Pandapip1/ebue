/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* realpath: open the file and ask the kernel what it is called.  A path
 * that does not exist cannot be canonicalised that way, so it is an
 * ENOENT like POSIX says.
 *
 * DELIBERATELY LEFT AN NT-ONLY FRONT DOOR, not behind the platform-
 * abstraction seam (src/internal/plat_unistd.h etc) at all -- documented
 * here rather than silently, matching this migration's own standard
 * (see src/internal/plat_fcntl.h's history: "honest partial progress
 * with a documented gap beats a forced, fragile fix").
 *
 * The __vfs_resolve_at() call below IS the same NT-only-overlay call
 * chdir()/readlinkat() used to make directly from their own front doors
 * before being moved behind __plat_chdir()/__plat_readlink() -- but
 * moving just that one call here would not make this function portable,
 * because everything AFTER it depends just as completely on __handle_path()
 * (src/internal/path.c), which does not merely interpret an NT status the
 * way __ntpath_at()/__vfs_resolve_at() do: it takes a raw NT `HANDLE`
 * (not the abstract __plat_handle_t src/internal/plat_handle.h's whole
 * seam is built around) and asks NT itself what path a handle was
 * opened through -- there is no POSIX or Linux equivalent call, and a
 * real Linux backend would need an entirely different algorithm (e.g.
 * readlink() on /proc/self/fd/N, or the openat2(2) RESOLVE_* flags),
 * not a translation of this one. __handle_path() also has three other
 * NT-only callers (src/internal/vfs.c, src/stat/chmod.c,
 * src/process/exec.c) that would need the same treatment for a Linux
 * realpath() to mean anything -- restructuring only this front door
 * would be a forced, fragile fix for a small fraction of the real gap.
 * Left as future work; the whole function stays exactly as it always
 * was, calling __vfs_resolve_at()/__handle_path() directly. */
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
