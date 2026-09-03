/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later */

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "libc.h"

int ttyname_r(int fd, char *buf, size_t len)
{
	if (!isatty(fd)) return errno;
	if (len < 5) return ERANGE;
	memcpy(buf, "CON", 4);
	return 0;
}

char *ttyname(int fd)
{
	static char buf[8];
	if (ttyname_r(fd, buf, sizeof buf)) return 0;
	return buf;
}

/* tcgetpgrp()/tcsetpgrp(). This platform has exactly one session and one
 * process group, so there's never a second group to report or move to
 * -- but that fixed answer still requires the shall-fail EBADF for an
 * invalid fildes, hence __fd_get(). The gate is __fd_get() alone, not
 * get_console(): the one-process-group model is a property of the
 * process, not a particular descriptor, so it answers for any fd this
 * process holds, console or not (test/posix-unistd.c pins this with
 * stdin on /dev/null). Whether a non-console fildes should get ENOTTY
 * instead is a separate, still-open question (test/POSIX-COVERAGE.md). */
pid_t tcgetpgrp(int fd)
{
	if (!__fd_get(fd)) return -1;	/* EBADF, already set */
	return getpgrp();
}

int tcsetpgrp(int fd, pid_t p) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	if (!__fd_get(fd)) return -1;	/* EBADF, already set */
	(void)p;			/* the only group there is; nothing moves */
	return 0;
}
