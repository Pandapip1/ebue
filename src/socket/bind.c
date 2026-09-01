/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * bind(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * bind.html.  "assign a local socket address to a socket" (DESCRIPTION);
 * EINVAL "the socket is already bound" (ERRORS) -- checked here via the
 * __SOCK_ST_BOUND bit (src/internal/plat_socket.h) rather than trusting
 * either backend's own error for a rebind, since neither's behaviour on
 * an already-bound endpoint was verified against a real reference (AFD's
 * against real Windows; Linux's own EINVAL-on-rebind was not relied on
 * either, for the same reason: consistency between backends matters more
 * than which one technically already gets this right).
 *
 * ReactOS's WSPBind (dll/win32/msafd/misc/dllmain.c) picks
 * AFD_SHARE_EXCLUSIVE/REUSE/WILDCARD/UNIQUE from ExclusiveAddressUse/
 * wildcard-endpoint-info/ReuseAddresses; this project only implements
 * the one option in <sys/socket.h>'s setsockopt() scope, SO_REUSEADDR
 * (src/socket/sockopt.c sets __SOCK_ST_REUSEADDR), so the choice handed
 * to __plat_socket_bind() is just a plain reuse-if-requested boolean --
 * which AFD_SHARE_* value (or, on Linux, which setsockopt(2) call) that
 * becomes is each backend's own affair (src/socket/{nt,linux}/
 * plat_socket.c).
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include "libc.h"
#include "plat_socket.h"

int bind(int fd, const struct sockaddr *addr, socklen_t len)
{
	struct __fd *f = __fd_get(fd);

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (f->pad & (__SOCK_ST_BOUND | __SOCK_ST_CONNECTED | __SOCK_ST_LISTENING)) { errno = EINVAL; return -1; }

	if (__plat_socket_bind(f->h, (f->pad & __SOCK_ST_REUSEADDR) ? 1 : 0, addr, len) < 0) return -1;

	f->pad |= __SOCK_ST_BOUND;
	return 0;
}

// NOLINTEND(misc-include-cleaner)
