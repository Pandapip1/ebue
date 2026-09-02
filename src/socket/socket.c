/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * socket(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * socket.html.  "create an unbound socket in a communications domain,
 * and return a file descriptor" (DESCRIPTION) -- AF_INET/SOCK_STREAM
 * only (this project's declared scope; see <sys/socket.h>'s banner),
 * everything else is EAFNOSUPPORT/EPROTOTYPE/EPROTONOSUPPORT
 * (mandatory ERRORS).
 *
 * SOCK_CLOEXEC (same page, DESCRIPTION's "type" paragraph): the bit
 * lives in `type` alongside the socket type itself, so it is masked
 * off before the SOCK_STREAM comparison and folded into the new fd's
 * flags word instead -- the same close-on-exec plumbing open() already
 * uses (src/fcntl/open.c, src/internal/fd.c's exec-time sweep), just
 * reached from socket() instead of open().  See <sys/socket.h>'s own
 * comment on the macro for why it reuses O_CLOEXEC's bit value.
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

	if (domain != AF_INET) { errno = EAFNOSUPPORT; return -1; }
	if ((type & ~SOCK_CLOEXEC) != SOCK_STREAM) { errno = EPROTOTYPE; return -1; }
	if (protocol != 0 && protocol != IPPROTO_TCP) { errno = EPROTONOSUPPORT; return -1; }

	if (__plat_socket_open(&h) < 0) return -1;

	fd = __fd_install(h, cloexec ? O_CLOEXEC : 0, __FD_SOCKET);
	if (fd < 0) { __plat_close(h); return -1; }
	/* Same residual as src/socket/accept.c's own __fd_get(newfd)->pad:
	 * not expressible via nonnull (fd is not a pointer, and __fd_get()'s
	 * return is a local, not a parameter), never NULL in practice since
	 * fd just came back from a successful __fd_install(). */
	__fd_get(fd)->pad = 0;
	return fd;
}

// NOLINTEND(misc-include-cleaner)
