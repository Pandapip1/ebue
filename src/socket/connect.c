/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * connect(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * connect.html.  "If the initiating socket is not bound, it will be
 * bound to an address selected by the transport layer" (DESCRIPTION,
 * the AF_UNIX exception does not apply here) -- implemented as an
 * explicit wildcard bind() first, matching ReactOS's WSPConnect
 * (dll/win32/msafd/misc/dllmain.c: "Bind us First" / WSHGetWildcardSockaddr)
 * doing the same before IOCTL_AFD_CONNECT.  SOCK_NONBLOCK's O_NONBLOCK
 * bit is stored and reported honestly on the fd (<sys/socket.h>'s own
 * comment on the macro), but this function does not yet consult it: the
 * ioctl is always waited to completion here, never returning
 * EINPROGRESS/EALREADY, whether or not the socket was opened
 * non-blocking. A disclosed scope boundary, not a bug -- making
 * connect() itself honor the bit is later work.
 *
 * SOCK_DGRAM (2026-09-01): connect() applies to a datagram socket too
 * (connect.html DESCRIPTION: "If the socket ... is of type SOCK_DGRAM
 * ... this call ... sets up the default destination address"), so
 * nothing here is gated on __SOCK_ST_DGRAM.  The pre-existing EISCONN
 * gate just below (`f->pad & __SOCK_ST_CONNECTED`) is stricter than
 * POSIX requires for a datagram socket, which is allowed to connect()
 * again to change its peer, or to AF_UNSPEC to dissolve it entirely --
 * this project does not implement that reconnection path (no caller
 * needs it: every SOCK_DGRAM socket this project hands out is either
 * still unconnected or was connect()'d exactly once by socketpair.c and
 * never again), so a second connect() attempt reports EISCONN rather
 * than being silently wrong about what it does.  A real per-datagram
 * destination path (sendto() with a real dest_addr on an unconnected
 * socket) remains unimplemented for the same reason -- see
 * sendrecv.c's banner. */
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "plat_socket.h"

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
	struct __fd *f = __fd_get(fd);
	const struct sockaddr *restrict peer = addr;

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
	memcpy(f->peer, peer, sizeof(struct sockaddr_in));
	f->peer_len = sizeof(struct sockaddr_in);
	return 0;
}
