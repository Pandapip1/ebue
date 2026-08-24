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
#include "afd.h"

int listen(int fd, int backlog)
{
	struct __fd *f = __fd_get(fd);
	AFD_LISTEN_DATA ld;
	NTSTATUS st;

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (f->pad & AFD_ST_CONNECTED) { errno = EINVAL; return -1; }

	if (f->pad & AFD_ST_LISTENING) return 0; /* listen.html doesn't forbid a repeat call */

	if (!(f->pad & AFD_ST_BOUND)) {
		struct sockaddr_in wild;
		memset(&wild, 0, sizeof(wild));
		wild.sin_family = AF_INET;
		wild.sin_addr.s_addr = INADDR_ANY;
		if (bind(fd, (struct sockaddr *)&wild, sizeof(wild)) < 0) return -1;
	}

	if (backlog < 0) backlog = 0;
	if (backlog > SOMAXCONN) backlog = SOMAXCONN;

	ld.UseSAN = 0;
	ld.UseDelayedAcceptance = 0;
	ld.Backlog = (unsigned long)backlog;

	st = __afd_ioctl(f->h, IOCTL_AFD_START_LISTEN, &ld, sizeof(ld), 0, 0, 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	f->pad |= AFD_ST_LISTENING;
	return 0;
}
