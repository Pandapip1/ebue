/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * daemon(): the standard BSD/glibc fork()+setsid() idiom. Previously
 * left undefined-ok because fork() (its whole foundation) "needs a
 * patched Wine to run at all" -- true of running this project's own
 * *-win.c fork tests under an *unpatched* Wine in CI (CONTRIBUTING.md),
 * not a statement about fork() itself: src/process/fork.c's fork() is a
 * real, working implementation (RtlCloneUserProcess-backed), already
 * used by vfork() (src/unistd/vfork.c) and exercised directly by this
 * tree's own *-win.c fork tests against real Windows. daemon() needed
 * nothing new, just to be written on top of what already exists:
 * fork(), setsid() (src/unistd/ids.c), chdir()
 * (src/unistd/chdir.c) and open()+dup2() (src/fcntl/open.c,
 * src/unistd/dup.c) for the /dev/null redirect.
 *
 * daemon(3) DESCRIPTION (BSD; not in POSIX): fork() a child that
 * outlives the caller (the parent calls _exit(0) once fork() succeeds,
 * the standard "detach" shape); in the child, setsid() to drop the
 * controlling terminal and become a session/process-group leader; then,
 * unless nochdir, chdir("/") (so the daemon does not pin whatever
 * filesystem it happened to start in); then, unless noclose, redirect
 * fd 0/1/2 to /dev/null (so the daemon does not implicitly depend on,
 * or accidentally write output to, whatever console/pipe it started
 * with). Returns 0 in the child on success, -1/errno on failure -- the
 * parent, on a successful fork(), never returns from daemon() at all. */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include "libc.h"

int daemon(int nochdir, int noclose)
{
	pid_t pid = fork();

	if (pid < 0) return -1;
	if (pid > 0) _exit(0);	/* parent: detach, never returning here */

	/* Child, from here on: a setsid() failure is real and reportable
	 * (unlike the parent branch above, this process is still the one
	 * that called daemon() and can still return to its caller). A
	 * fresh child's pgid is inherited from its parent (never equal to
	 * its own brand-new pid), so this should not actually fail in
	 * practice -- see src/unistd/ids.c's setsid() for the one
	 * documented way it can. */
	if (setsid() < 0) return -1;

	/* chdir("/") failing is worth reporting too: the caller asked not
	 * to be left in a stale/unmounted working directory, and silently
	 * leaving it there would be exactly that. */
	if (!nochdir && chdir("/") < 0) return -1;

	if (!noclose) {
		int fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			/* dup2(fd, fd) is a documented no-op (returns fd
			 * without closing it), so this is correct even if
			 * open() itself happened to hand back 0, 1 or 2. */
			if (dup2(fd, STDIN_FILENO) < 0 ||
			    dup2(fd, STDOUT_FILENO) < 0 ||
			    dup2(fd, STDERR_FILENO) < 0)
				return -1;
			if (fd > STDERR_FILENO) close(fd);
		}
		/* open() failing here is not itself daemon()'s failure --
		 * glibc's own daemon() takes the same view (the whole
		 * redirect block is skipped, not surfaced, if /dev/null
		 * cannot be opened); nothing else this function did needs
		 * undoing for a caller who declines the redirect to matter. */
	}
	return 0;
}

// NOLINTEND(misc-include-cleaner)
