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

	/* Built through src/internal/afd.h's AFD_CONNECT_REQ_OFF_*, not
	 * through AFD_CONNECT_INFO's members: see that header's connect
	 * banner for why the address's offset is pointer-sized, and why
	 * ReactOS's AFD_CONNECT_INFO puts it 12 bytes too early on
	 * x86_64.  `ci` is only the (correctly aligned, large enough)
	 * storage. */
	memset(&ci, 0, sizeof(ci));
	if (__afd_build_connect_request(&ci, addr, len) < 0) return -1;

	/* __afd_connect_request_size(), not sizeof(ci): IOCTL_AFD_CONNECT
	 * is METHOD_NEITHER, so the declared length is what afd.sys
	 * bounds its read of the address by, and sizeof() rounds up. */
	st = __afd_ioctl(f->h, IOCTL_AFD_CONNECT, &ci, (ULONG)__afd_connect_request_size(), 0, 0, 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	f->pad |= AFD_ST_CONNECTED;
	memcpy(f->peer, addr, sizeof(struct sockaddr_in));
	f->peer_len = sizeof(struct sockaddr_in);
	return 0;
}
