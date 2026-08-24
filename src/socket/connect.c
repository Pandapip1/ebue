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
#include "afd.h"

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
	struct __fd *f = __fd_get(fd);
	AFD_CONNECT_INFO ci;
	NTSTATUS st;

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (f->pad & AFD_ST_CONNECTED) { errno = EISCONN; return -1; }
	if (f->pad & AFD_ST_LISTENING) { errno = EOPNOTSUPP; return -1; }

	if (!(f->pad & AFD_ST_BOUND)) {
		struct sockaddr_in wild;
		memset(&wild, 0, sizeof(wild));
		wild.sin_family = AF_INET;
		wild.sin_addr.s_addr = INADDR_ANY;
		if (bind(fd, (struct sockaddr *)&wild, sizeof(wild)) < 0) return -1;
	}

	memset(&ci, 0, sizeof(ci));
	ci.UseSAN = 0;
	ci.Root = 0;
	ci.Unknown = 0;
	if (__afd_addr_from_sockaddr(addr, len, &ci.RemoteAddress) < 0) return -1;

	st = __afd_ioctl(f->h, IOCTL_AFD_CONNECT, &ci, sizeof(ci), 0, 0, 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	f->pad |= AFD_ST_CONNECTED;
	return 0;
}
