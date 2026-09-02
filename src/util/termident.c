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

/* Describes fd, if it is a terminal at all.  Returns 1 (out filled) or
 * 0 (out left untouched -- caller tries the next candidate fd). */
static int describe_fd(int fd, struct term_ident *out)
{
	struct stat st;
	char procpath[32];
	ssize_t n;

	/* Linux: a real device node, resolved for real -- see
	 * termident.h's banner for why this is tried before isatty(). */
	if (fstat(fd, &st) == 0 && S_ISCHR(st.st_mode)) {
		snprintf(procpath, sizeof procpath, "/proc/self/fd/%d", fd);
		n = readlink(procpath, out->path, sizeof out->path - 1);
		if (n > 0) {
			out->path[n] = 0;
			out->opaque = 0;
			set_shortname(out, out->path);
			return 1;
		}
		/* readlink() failing here (no /proc -- NT, or a Linux
		 * process started with procfs unmounted) is not itself
		 * proof fd is not a terminal; fall through to isatty(). */
	}

	/* NT (or any Linux fd whose real path this library could not
	 * resolve above): isatty()'s own real answer, opaque -- no
	 * writable permission-bit backing reachable through this path. */
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
