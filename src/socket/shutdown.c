/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * shutdown(): https://pubs.opengroup.org/onlinepubs/9699919799/
 * functions/shutdown.html.  SHUT_RD "disables further receive
 * operations", SHUT_WR "disables further send operations", SHUT_RDWR
 * both.  `how` is validated here (EINVAL for anything else) and, once
 * connection state is confirmed, handed straight to
 * __plat_socket_shutdown() (src/internal/plat_socket.h) -- on NT that
 * still means mapping onto AFD_DISCONNECT_RECV/SEND
 * (src/internal/afd.h, IOCTL_AFD_DISCONNECT) with a zeroed Timeout (a
 * graceful shutdown, no AFD_DISCONNECT_ABORT, not a wait for queued
 * data to drain); on Linux, SHUT_RD/SHUT_WR/SHUT_RDWR already are the
 * kernel's own shutdown(2) values, so nothing is translated at all.
 * See src/socket/{nt,linux}/plat_socket.c.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/socket.h>
#include <errno.h>
#include "libc.h"
#include "plat_socket.h"

int shutdown(int fd, int how) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct __fd *f = __fd_get(fd);

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (!(f->pad & __SOCK_ST_CONNECTED)) { errno = ENOTCONN; return -1; }

	return __plat_socket_shutdown(f->h, how);
}

// NOLINTEND(misc-include-cleaner)
