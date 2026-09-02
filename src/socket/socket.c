/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * socket(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * socket.html.  "create an unbound socket in a communications domain,
 * and return a file descriptor" (DESCRIPTION) -- this project's
 * declared scope (see <sys/socket.h>'s banner) is AF_INET/SOCK_STREAM,
 * AF_INET/SOCK_DGRAM, and an anonymous AF_UNIX/SOCK_DGRAM; everything
 * else is EAFNOSUPPORT/EPROTOTYPE/EPROTONOSUPPORT (mandatory ERRORS).
 *
 * AF_UNIX/SOCK_DGRAM (2026-09-01): this project has no <sys/un.h> (no
 * pathname-bearing AF_UNIX address exists to bind() one to), so what
 * socket(AF_UNIX, SOCK_DGRAM, 0) hands back is, underneath, the exact
 * same AF_INET/SOCK_DGRAM (UDP) endpoint socket(AF_INET, SOCK_DGRAM, 0)
 * would -- unbound, unconnected, usable with this project's own
 * AF_INET sockaddr_in if the caller bind()s/connect()s it explicitly,
 * and it is what src/socket/socketpair.c's own AF_UNIX/SOCK_DGRAM path
 * builds a connected pair out of.  This is the same standing precedent
 * socketpair.c's AF_UNIX/SOCK_STREAM path already established for
 * accept()ed/connect()ed peers reporting AF_INET addresses rather than
 * AF_UNIX ones -- not a new kind of shortcut.
 *
 * SOCK_CLOEXEC (same page, DESCRIPTION's "type" paragraph): the bit
 * lives in `type` alongside the socket type itself, so it is masked
 * off before the SOCK_STREAM/SOCK_DGRAM comparison and folded into the
 * new fd's flags word instead -- the same close-on-exec plumbing open()
 * already uses (src/fcntl/open.c, src/internal/fd.c's exec-time sweep),
 * just reached from socket() instead of open().  See <sys/socket.h>'s
 * own comment on the macro for why it reuses O_CLOEXEC's bit value.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include "libc.h"
#include "plat_socket.h"
#include "plat_fd.h"

int socket(int domain, int type, int protocol) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	__plat_handle_t h;
	int fd;
	int cloexec = type & SOCK_CLOEXEC;
	int t = type & ~SOCK_CLOEXEC;

	if (domain != AF_INET && domain != AF_UNIX) { errno = EAFNOSUPPORT; return -1; }
	if (t != SOCK_STREAM && t != SOCK_DGRAM) { errno = EPROTOTYPE; return -1; }
	/* AF_UNIX/SOCK_STREAM: not one of the two pairs this project
	 * creates (see this file's banner) -- AF_UNIX/SOCK_STREAM only
	 * exists internally, inside socketpair.c's own loopback-TCP
	 * construction, never reached through this front door.  EPROTONOSUPPORT,
	 * not EAFNOSUPPORT: the family is real, this exact family/type
	 * combination is what is missing (ERRORS: "The protocol is not
	 * supported by the address family"). */
	if (domain == AF_UNIX && t != SOCK_DGRAM) { errno = EPROTONOSUPPORT; return -1; }
	if (t == SOCK_STREAM && protocol != 0 && protocol != IPPROTO_TCP) { errno = EPROTONOSUPPORT; return -1; }
	if (t == SOCK_DGRAM && domain == AF_INET && protocol != 0 && protocol != IPPROTO_UDP) { errno = EPROTONOSUPPORT; return -1; }
	if (t == SOCK_DGRAM && domain == AF_UNIX && protocol != 0) { errno = EPROTONOSUPPORT; return -1; }

	if (__plat_socket_open(&h, t) < 0) return -1;

	fd = __fd_install(h, cloexec ? O_CLOEXEC : 0, __FD_SOCKET);
	if (fd < 0) { __plat_close(h); return -1; }
	/* Same residual as src/socket/accept.c's own __fd_get(newfd)->pad:
	 * not expressible via nonnull (fd is not a pointer, and __fd_get()'s
	 * return is a local, not a parameter), never NULL in practice since
	 * fd just came back from a successful __fd_install(). */
	__fd_get(fd)->pad = (t == SOCK_DGRAM) ? __SOCK_ST_DGRAM : 0;
	return fd;
}

// NOLINTEND(misc-include-cleaner)
