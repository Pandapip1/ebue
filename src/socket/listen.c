/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * listen(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * listen.html.  "mark a connection-mode socket...as accepting
 * connections" (DESCRIPTION); a negative backlog is treated as 0, and
 * values are clamped to SOMAXCONN ("implementations may impose a limit
 * on backlog and silently reduce the specified value") -- both handled
 * here.  EDESTADDRREQ ("socket is not bound...and the protocol requires
 * that it be") applies to a not-yet-bound stream socket, so an implicit
 * bind to the wildcard address happens first, matching ReactOS's
 * WSPConnect (dllmain.c) doing the same before connect() -- listen()
 * needs the identical auto-bind for the same reason: AFD requires a
 * bound endpoint before either IOCTL_AFD_CONNECT or
 * IOCTL_AFD_START_LISTEN.
 */
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
