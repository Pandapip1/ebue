/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * connect(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * connect.html.  "If the initiating socket is not bound, it will be
 * bound to an address selected by the transport layer" (DESCRIPTION,
 * the AF_UNIX exception does not apply here) -- implemented as an
 * explicit wildcard bind() first, matching ReactOS's WSPConnect
 * (dll/win32/msafd/misc/dllmain.c: "Bind us First" / WSHGetWildcardSockaddr)
 * doing the same before IOCTL_AFD_CONNECT.  This project only ever opens
 * sockets non-blocking-unaware (no O_NONBLOCK/fcntl wiring for sockets
 * yet -- left for a later stage, see the top-level report), so this is
 * always the blocking form: the ioctl is waited to completion, never
 * returning EINPROGRESS/EALREADY.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "plat_socket.h"
#include "ownership_stubs.h"

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
	struct __fd *f = __fd_get(fd);

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (f->pad & __SOCK_ST_CONNECTED) { errno = EISCONN; return -1; }
	if (f->pad & __SOCK_ST_LISTENING) { errno = EOPNOTSUPP; return -1; }

	if (!(f->pad & __SOCK_ST_BOUND)) {
		struct sockaddr_in wild;
		memset(&wild, 0, sizeof(wild));
		wild.sin_family = AF_INET;
		wild.sin_addr.s_addr = INADDR_ANY;
		/* A recursive call to this file's own bind() front door, not
		 * a direct __plat_socket_bind(): bind() is itself fully
		 * portable after this refactor (src/socket/bind.c), so
		 * calling it recursively reruns its ENOTSOCK/already-bound
		 * checks and its f->pad |= __SOCK_ST_BOUND bookkeeping for
		 * free, exactly like the pre-refactor NT-only version did.
		 * There is no NT-specific step here to relocate. */
		if (bind(fd, (struct sockaddr *)&wild, sizeof(wild)) < 0) return -1;
	}

	if (__plat_socket_connect(f->h, addr, len) < 0) return -1;

	f->pad |= __SOCK_ST_CONNECTED;
	__ownership_writable_span(f->peer, sizeof(struct sockaddr_in));
	__ownership_readable_span(addr, sizeof(struct sockaddr_in));
	memcpy(f->peer, addr, sizeof(struct sockaddr_in));
	f->peer_len = sizeof(struct sockaddr_in);
	return 0;
}
