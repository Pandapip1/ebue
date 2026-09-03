/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * listen(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * listen.html.  A negative backlog is treated as 0; values are clamped
 * to SOMAXCONN.  An unbound stream socket is implicitly wildcard-bound
 * first, matching ReactOS's WSPConnect doing the same before connect():
 * AFD requires a bound endpoint before IOCTL_AFD_START_LISTEN.
 *
 * SOCK_DGRAM: EOPNOTSUPP, checked here before either backend is reached
 * rather than relying on IOCTL_AFD_START_LISTEN or Linux's listen(2) to
 * report the same errno for a UDP fd -- only the front-door check is
 * verified and platform-independent by construction. */
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "plat_socket.h"

int listen(int fd, int backlog) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct __fd *f = __fd_get(fd);

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (f->pad & __SOCK_ST_DGRAM) { errno = EOPNOTSUPP; return -1; }
	if (f->pad & __SOCK_ST_CONNECTED) { errno = EINVAL; return -1; }

	if (f->pad & __SOCK_ST_LISTENING) return 0; /* listen.html doesn't forbid a repeat call */

	if (!(f->pad & __SOCK_ST_BOUND)) {
		struct sockaddr_in wild;
		memset(&wild, 0, sizeof(wild));
		wild.sin_family = AF_INET;
		wild.sin_addr.s_addr = INADDR_ANY;
		if (bind(fd, (struct sockaddr *)&wild, sizeof(wild)) < 0) return -1;
	}

	if (backlog < 0) backlog = 0;
	if (backlog > SOMAXCONN) backlog = SOMAXCONN;

	if (__plat_socket_listen(f->h, (unsigned long)backlog) < 0) return -1;

	f->pad |= __SOCK_ST_LISTENING;
	return 0;
}
