/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * socketpair(): a connected, bidirectional SOCK_STREAM pair.  ntlibc does
 * not expose pathname-bearing AF_UNIX endpoints, but socketpair has no
 * pathname or externally visible address.  A private loopback TCP listener
 * therefore has exactly its observable byte-stream semantics while reusing
 * the AF_INET transport the rest of this directory already implements.
 *
 * SOCK_CLOEXEC (sys_socket.h.html's DESCRIPTION carries the same "type
 * argument may set SOCK_CLOEXEC" text socket() cites): both ends of the
 * pair get it if the caller asked for it.  pair[0] gets it for free --
 * it comes from this file's own socket() call below, which already
 * honors the bit.  pair[1] does not: it comes from accept(), and this
 * project has no accept4() to pass the bit through, so it is applied
 * with a separate fcntl(F_SETFD) once the fd exists.  A window exists
 * between that accept() and this fcntl() in which a concurrent exec()
 * in another thread would leak the fd -- the same window accept4()
 * exists to close everywhere it is missing; nothing narrower is
 * available without it. */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

int socketpair(int domain, int type, int protocol, int pair[2]) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	struct sockaddr_in address;
	socklen_t length;
	int listener = -1, client = -1, server = -1;
	int saved;
	int cloexec = type & SOCK_CLOEXEC;

	if (domain != AF_UNIX) { errno = EAFNOSUPPORT; return -1; }
	if ((type & ~SOCK_CLOEXEC) != SOCK_STREAM) { errno = EPROTOTYPE; return -1; }
	if (protocol != 0) { errno = EPROTONOSUPPORT; return -1; }
	if (!pair) { errno = EINVAL; return -1; }

	listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0) goto fail;
	memset(&address, 0, sizeof address);
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(listener, (struct sockaddr *)&address, sizeof address) < 0 ||
	    listen(listener, 1) < 0) goto fail;
	length = sizeof address;
	if (getsockname(listener, (struct sockaddr *)&address, &length) < 0) goto fail;

	client = socket(AF_INET, SOCK_STREAM | cloexec, 0);
	if (client < 0 ||
	    connect(client, (struct sockaddr *)&address, sizeof address) < 0)
		goto fail;
	server = accept(listener, 0, 0);
	if (server < 0) goto fail;
	if (cloexec && fcntl(server, F_SETFD, FD_CLOEXEC) < 0) goto fail;
	(void)close(listener);
	pair[0] = client;
	pair[1] = server;
	return 0;

fail:
	saved = errno;
	if (server >= 0) (void)close(server);
	if (client >= 0) (void)close(client);
	if (listener >= 0) (void)close(listener);
	errno = saved;
	return -1;
}
