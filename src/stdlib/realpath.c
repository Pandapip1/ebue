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

	if (!path) { errno = EINVAL; return 0; }
	if (!*path) { errno = ENOENT; return 0; }
	fd = open(path, O_RDONLY);
	if (fd < 0) {
		/* a directory might refuse O_RDONLY; try it as one */
		if (errno == EISDIR || errno == EACCES) fd = open(path, O_RDONLY | O_DIRECTORY);
		if (fd < 0) { if (errno != EACCES) errno = ENOENT; return 0; }
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
