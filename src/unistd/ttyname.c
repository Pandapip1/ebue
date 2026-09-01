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

/* tcgetpgrp()/tcsetpgrp().  The *answer* is fixed: this platform has
 * exactly one session and one process group (src/unistd/ids.c's
 * getpgrp()/getsid(), and src/termios/termios.c's banner for why a
 * console cannot have a foreground/background split), so there is never
 * a second group for tcgetpgrp() to report or for tcsetpgrp() to move
 * the terminal to.  A fixed answer is not the same thing as no argument
 * check, though, and these two used to discard `fd` entirely:
 * tcgetpgrp.html and tcsetpgrp.html both list "[EBADF] The fildes
 * argument is not a valid file descriptor" as a *shall* fail, so fildes
 * goes through __fd_get() like every other fd-taking call here.
 *
 * The gate is __fd_get() alone, deliberately, and not
 * src/termios/termios.c's get_console(): the one-process-group model
 * above is a property of the process, not of a particular descriptor,
 * so it answers for any descriptor this process actually holds --
 * test/posix-unistd.c pins that on descriptors that are demonstrably
 * not consoles (`make check` runs with stdin on /dev/null).  Adding
 * [ENOTTY] for a non-console fildes would narrow that model, which is a
 * separate decision from supplying the argument check it never had; see
 * test/POSIX-COVERAGE.md's unistd.h section, which records it as open. */
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
