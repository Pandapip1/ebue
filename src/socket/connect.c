/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * connect(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * connect.html.  An unbound socket is bound to a transport-selected
 * address first, via an explicit wildcard bind(), matching ReactOS's
 * WSPConnect "Bind us First" behavior before IOCTL_AFD_CONNECT.
 * SOCK_NONBLOCK's O_NONBLOCK bit is stored and reported honestly on the
 * fd but not yet consulted here: the ioctl always runs to completion,
 * never returning EINPROGRESS/EALREADY. A disclosed scope boundary, not
 * a bug.
 *
 * connect() also applies to SOCK_DGRAM (sets the default destination),
 * so nothing here is gated on __SOCK_ST_DGRAM. The EISCONN gate below is
 * stricter than POSIX requires for a datagram socket (which may
 * reconnect, or disconnect via AF_UNSPEC) -- that path is unimplemented
 * since no caller needs it, so a second connect() reports EISCONN rather
 * than doing the wrong thing silently. */
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
		/* Recurses into this file's own bind() rather than calling
		 * __plat_socket_bind() directly, reusing its checks and
		 * __SOCK_ST_BOUND bookkeeping for free. */
		if (bind(fd, (struct sockaddr *)&wild, sizeof(wild)) < 0) return -1;
	}

	if (__plat_socket_connect(f->h, addr, len) < 0) return -1;

	f->pad |= __SOCK_ST_CONNECTED;
	memcpy(f->peer, peer, sizeof(struct sockaddr_in));
	f->peer_len = sizeof(struct sockaddr_in);
	return 0;
}
