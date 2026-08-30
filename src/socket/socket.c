/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * socket(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * socket.html.  "create an unbound socket in a communications domain,
 * and return a file descriptor" (DESCRIPTION) -- AF_INET/SOCK_STREAM
 * only (this project's declared scope; see <sys/socket.h>'s banner),
 * everything else is EAFNOSUPPORT/EPROTOTYPE/EPROTONOSUPPORT
 * (mandatory ERRORS).
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include "libc.h"
#include "plat_socket.h"
#include "plat_fd.h"

int socket(int domain, int type, int protocol)
{
	__plat_handle_t h;
	int fd;

	if (domain != AF_INET) { errno = EAFNOSUPPORT; return -1; }
	if (type != SOCK_STREAM) { errno = EPROTOTYPE; return -1; }
	if (protocol != 0 && protocol != IPPROTO_TCP) { errno = EPROTONOSUPPORT; return -1; }

	if (__plat_socket_open(&h) < 0) return -1;

	fd = __fd_install(h, 0, __FD_SOCKET);
	if (fd < 0) { __plat_close(h); return -1; }
	__fd_get(fd)->pad = 0;
	return fd;
}
