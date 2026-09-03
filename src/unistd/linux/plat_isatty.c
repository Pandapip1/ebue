/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Real Linux isatty(), replacing src/unistd/isatty.c's NT-only body
 * (that file's own banner, and the Makefile's PLATFORM axis -- see its
 * own comment) the same way src/termios/linux/plat_termios.c replaces
 * src/termios/termios.c's body.
 *
 * WHY THE NT ANSWER (a static `f->type == __FD_CONSOLE` check) DOES NOT
 * WORK HERE: src/internal/linux/plat_fd_init.c's classify_fd() folds
 * every character device -- a real pty slave included -- into the one
 * __FD_CHAR bucket; __FD_CONSOLE is never assigned to anything on this
 * platform (grep confirms zero writes to it outside NT-only files). A
 * static classification decided once at fd-creation time also can't
 * ever be complete on its own terms: an fd that reaches this process by
 * dup(), by surviving exec(), or via SCM_RIGHTS carries no record of
 * how it was originally opened, the identical gap src/util/termident.c's
 * own describe_fd() fix (path_looks_like_tty(), same commit series)
 * exists to route around for ITS one caller by resolving a path instead
 * -- but a resolved path is still only evidence about a name, not the
 * fd's real live capability, and unlike this file's own caller,
 * termident.c only ever needs an answer for fd 0/1/2 in one process, not
 * a general isatty() every caller in this tree relies on.
 *
 * WHAT WORKS INSTEAD, THE SAME WAY EVERY OTHER LIBC ANSWERS THIS: a
 * live capability probe against the fd itself, right now, regardless of
 * how it got here. tcgetattr() (src/termios/linux/plat_termios.c) is
 * already exactly that probe -- a real ioctl(TCGETS2) against the
 * fd, gated on nothing but __fd_get() (EBADF) and the kernel's own
 * ioctl_tty(2) dispatch (ENOTTY off anything that is not a real
 * terminal, straight from the kernel, that file's own "GATING" banner
 * clause). Calling it here is not a workaround or an approximation of
 * isatty() -- it is the same technique glibc/musl isatty() use
 * internally (tcgetattr()/ioctl(fd, TCGETS, ...) succeeding IS the
 * POSIX definition of "refers to a terminal"), reusing already-real,
 * already-tested plumbing rather than duplicating a second raw
 * ioctl(TCGETS2) call site.
 *
 * tcgetattr() already leaves errno exactly where isatty() needs it on
 * both failure paths (EBADF from __fd_get(), ENOTTY from the ioctl) --
 * see isatty.html ERRORS, which lists only those two -- so there is
 * nothing left for this function to set itself. */
#include <unistd.h>
#include <termios.h>

int isatty(int fd)
{
	struct termios t;
	return tcgetattr(fd, &t) == 0;
}
