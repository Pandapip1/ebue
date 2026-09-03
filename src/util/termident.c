/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * See src/util/termident.h for what this answers, why it exists
 * separately from isatty(), and exactly what is real on each platform.
 */
#include "termident.h"
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

static void set_shortname(struct term_ident *out, const char *s)
{
	const char *base = s;
	size_t n;
	if (strncmp(base, "/dev/", 5) == 0) base += 5;
	n = strlen(base);
	if (n >= sizeof out->shortname) n = sizeof out->shortname - 1;
	(void)snprintf(out->shortname, sizeof out->shortname, "%.*s", (int)n,
	    base);
}

/* S_ISCHR() alone does not mean "terminal" -- /dev/null, /dev/zero,
 * and every other non-tty character device are S_ISCHR too.  Nor does
 * isatty() succeeding: it is a live ioctl(TCGETS)-style capability
 * probe (src/unistd/linux/plat_isatty.c) that also succeeds on a
 * Unix98 pty MASTER fd (/proc/self/fd resolves that one to the shared
 * "/dev/ptmx" multiplexer node, confirmed live), which is not a
 * per-session device this file's callers could chmod() or deliver a
 * write(1p) message into. A resolved device path is only really a
 * terminal in the sense describe_fd() below needs if it is one of the
 * shapes the Linux kernel actually hands out for real ttys: a pty
 * slave (/dev/pts/N), a virtual console or serial line (/dev/ttyN,
 * /dev/ttyS0, ...), or the /dev/tty and /dev/console aliases
 * themselves -- deliberately excluding /dev/ptmx itself. */
static int path_looks_like_tty(const char *path)
{
	static const char *const tty_prefixes[] = {
		"/dev/pts/",
		"/dev/tty",
		"/dev/console",
	};
	size_t i;
	for (i = 0; i < sizeof tty_prefixes / sizeof tty_prefixes[0]; i++) {
		size_t len = strlen(tty_prefixes[i]);
		if (strncmp(path, tty_prefixes[i], len) == 0) return 1;
	}
	return 0;
}

/* Describes fd, if it is a terminal at all.  Returns 1 (out filled) or
 * 0 (out left untouched -- caller tries the next candidate fd). */
static int describe_fd(int fd, struct term_ident *out)
{
	struct stat st;
	char procpath[32];
	ssize_t n;

	/* Linux: a real device node, resolved for real, checked against
	 * path_looks_like_tty()'s own narrower shape test -- see
	 * termident.h's banner for why this is tried before isatty()
	 * (isatty() answers a real but wider question than this file
	 * needs: it is also true of a ptmx master fd, which this path
	 * check correctly excludes). */
	if (fstat(fd, &st) == 0 && S_ISCHR(st.st_mode)) {
		snprintf(procpath, sizeof procpath, "/proc/self/fd/%d", fd);
		n = readlink(procpath, out->path, sizeof out->path - 1);
		if (n > 0) {
			out->path[n] = 0;
			if (path_looks_like_tty(out->path)) {
				out->opaque = 0;
				set_shortname(out, out->path);
				return 1;
			}
			/* A real, resolved char-device path that is not
			 * tty-shaped (/dev/null, /dev/zero, /dev/ptmx, ...):
			 * not a nameable terminal for this file's purposes.
			 * Fall through to isatty() below, same as any other
			 * non-terminal fd. */
		}
		/* readlink() failing here (no /proc -- NT, or a Linux
		 * process started with procfs unmounted) is not itself
		 * proof fd is not a terminal; fall through to isatty(). */
	}

	/* NT (a console fd has no filesystem path to resolve above at
	 * all), or a Linux fd this library could not resolve a path for
	 * (no procfs): isatty()'s own real answer, opaque -- no writable
	 * permission-bit backing reachable through this path either way. */
	if (isatty(fd)) {
		char *n2 = ttyname(fd);
		out->path[0] = 0;
		out->opaque = 1;
		set_shortname(out, n2 ? n2 : "tty");
		return 1;
	}

	return 0;
}

int __util_find_terminal(struct term_ident *out)
{
	int fd;
	for (fd = 0; fd < 3; fd++) {
		memset(out, 0, sizeof *out);
		if (describe_fd(fd, out)) return fd;
	}
	return -1;
}
