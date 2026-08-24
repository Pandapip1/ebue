/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * bind(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * bind.html.  "assign a local socket address to a socket" (DESCRIPTION);
 * EINVAL "the socket is already bound" (ERRORS) -- checked here via the
 * AFD_ST_BOUND bit (src/internal/afd.h) rather than trusting AFD's own
 * error for a rebind, since IOCTL_AFD_BIND's behaviour on an
 * already-bound endpoint was not verified against real Windows.
 *
 * ReactOS's WSPBind (dll/win32/msafd/misc/dllmain.c) picks
 * AFD_SHARE_EXCLUSIVE/REUSE/WILDCARD/UNIQUE from ExclusiveAddressUse/
 * wildcard-endpoint-info/ReuseAddresses; this project only implements
 * the one option in <sys/socket.h>'s setsockopt() scope, SO_REUSEADDR
 * (src/socket/sockopt.c sets AFD_ST_REUSEADDR), so the choice here is
 * just REUSE-if-requested-else-UNIQUE.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include "libc.h"
#include "afd.h"

int bind(int fd, const struct sockaddr *addr, socklen_t len)
{
	struct __fd *f = __fd_get(fd);
	AFD_BIND_DATA bd;
	NTSTATUS st;

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (f->pad & (AFD_ST_BOUND | AFD_ST_CONNECTED | AFD_ST_LISTENING)) { errno = EINVAL; return -1; }
	if (__afd_addr_from_sockaddr(addr, len, &bd.Address) < 0) return -1;

	bd.ShareType = (f->pad & AFD_ST_REUSEADDR) ? AFD_SHARE_REUSE : AFD_SHARE_UNIQUE;

	st = __afd_ioctl(f->h, IOCTL_AFD_BIND, &bd, sizeof(bd), &bd, sizeof(bd), 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	f->pad |= AFD_ST_BOUND;
	return 0;
}
