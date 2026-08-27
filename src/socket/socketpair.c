/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * socketpair(): a connected, bidirectional SOCK_STREAM pair.  ntlibc does
 * not expose pathname-bearing AF_UNIX endpoints, but socketpair has no
 * pathname or externally visible address.  A private loopback TCP listener
 * therefore has exactly its observable byte-stream semantics while reusing
 * the AF_INET transport the rest of this directory already implements.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int socketpair(int domain, int type, int protocol, int pair[2])
{
	struct sockaddr_in address;
	socklen_t length;
	int listener = -1, client = -1, server = -1;
	int saved;

	if (domain != AF_UNIX) { errno = EAFNOSUPPORT; return -1; }
	if (type != SOCK_STREAM) { errno = EPROTOTYPE; return -1; }
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

	client = socket(AF_INET, SOCK_STREAM, 0);
	if (client < 0 ||
	    connect(client, (struct sockaddr *)&address, sizeof address) < 0)
		goto fail;
	server = accept(listener, 0, 0);
	if (server < 0) goto fail;
	close(listener);
	pair[0] = client;
	pair[1] = server;
	return 0;

fail:
	saved = errno;
	if (server >= 0) close(server);
	if (client >= 0) close(client);
	if (listener >= 0) close(listener);
	errno = saved;
	return -1;
}
