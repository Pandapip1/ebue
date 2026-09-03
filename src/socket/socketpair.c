/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * socketpair(): a connected, bidirectional AF_UNIX pair.
 *
 * Two constructions exist, tried in order:
 *
 *   1. __plat_socketpair() (src/internal/plat_socket.h): a real, native
 *      socketpair(2) where the platform's kernel has one.  Linux does
 *      -- see src/socket/linux/plat_socket.c -- and genuinely connects
 *      the two ends with the kernel's own mutual send/receive flow
 *      control, which matters for SOCK_DGRAM (see socketpair_dgram()'s
 *      comment below for why the fallback construction cannot
 *      reproduce it).
 *   2. Falls back to a private loopback AF_INET pair -- a TCP
 *      listener+accept for SOCK_STREAM, a bind()+connect() UDP pair
 *      for SOCK_DGRAM -- when __plat_socketpair() reports ENOSYS (NT:
 *      AFD has no native socketpair primitive at all).  ntlibc does
 *      not expose pathname-bearing AF_UNIX endpoints, but socketpair
 *      has no pathname or externally visible address, so this has
 *      exactly the observable semantics socketpair() promises while
 *      reusing the AF_INET transport the rest of this directory
 *      already implements.
 *
 * SOCK_CLOEXEC/SOCK_NONBLOCK (sys_socket.h.html's DESCRIPTION carries
 * the same "type argument may set SOCK_CLOEXEC"/SOCK_NONBLOCK text
 * socket() cites): both ends of the pair get whichever bits the caller
 * asked for, on every path.  See <sys/socket.h>'s own comment on
 * SOCK_NONBLOCK for what storing the bit does and does not change.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "plat_socket.h"
#include "plat_fd.h"

/* Installs a __plat_socketpair()'d handle as a connected __FD_SOCKET,
 * mirroring socket()'s own __fd_install() + pad bookkeeping
 * (src/socket/socket.c) and connect()'s __SOCK_ST_CONNECTED/peer
 * caching (src/socket/connect.c) -- this handle is *already* connected
 * (that is the entire point of a native socketpair(2)), so there is no
 * separate connect() call to run those through.
 *
 * f->peer: an unnamed AF_UNIX socket's peer has no meaningful address
 * to cache (a real AF_UNIX socketpair(2) pair is anonymous on both
 * ends -- Linux's own getpeername(2) on one reports sa_family=AF_UNIX
 * with an empty path), and this project's getpeername() is pure
 * struct __fd field access with no live kernel query behind it
 * (src/socket/getname.c's banner), so an all-zero AF_UNIX-family
 * sockaddr is cached rather than left at whatever __fd_install() left
 * the slot in -- honest (matches what a real unnamed peer looks like)
 * rather than a fabricated AF_INET address the way the loopback
 * fallback's peer already is. */
static int install_pair_handle(__plat_handle_t h, int dgram, int cloexec, int nonblock)
{
	struct __fd *f;
	struct sockaddr peer;
	int fd = __fd_install(h, (cloexec ? O_CLOEXEC : 0) | (nonblock ? O_NONBLOCK : 0), __FD_SOCKET);

	if (fd < 0) { __plat_close(h); return -1; }
	f = __fd_get(fd);
	f->pad = __SOCK_ST_BOUND | __SOCK_ST_CONNECTED | (dgram ? __SOCK_ST_DGRAM : 0);
	memset(&peer, 0, sizeof peer);
	peer.sa_family = AF_UNIX;
	memcpy(f->peer, &peer, sizeof peer);
	f->peer_len = sizeof peer;
	return fd;
}

/* SOCK_STREAM: pair[0] gets SOCK_CLOEXEC/SOCK_NONBLOCK for free -- it
 * comes from this file's own socket() call below, which already honors
 * both bits.  pair[1] does not: it comes from accept(), and this
 * project has no accept4() to pass either bit through, so both are
 * applied with separate fcntl() calls once the fd exists (F_SETFD for
 * SOCK_CLOEXEC, F_SETFL for SOCK_NONBLOCK).  A window exists between
 * that accept() and those fcntl() calls in which a concurrent exec() in
 * another thread would leak the fd, or a concurrent I/O call would
 * block when the caller asked for non-blocking -- the same window
 * accept4() exists to close everywhere it is missing; nothing narrower
 * is available without it. */
static int socketpair_stream(int cloexec, int nonblock, int pair[2])
{
	struct sockaddr_in address;
	socklen_t length;
	int listener = -1, client = -1, server = -1;
	int saved;

	listener = socket(AF_INET, SOCK_STREAM, 0);
	if (listener < 0) goto fail;
	memset(&address, 0, sizeof address);
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(listener, (struct sockaddr *)&address, sizeof address) < 0 ||
	    listen(listener, 1) < 0) goto fail;
	length = sizeof address;
	if (getsockname(listener, (struct sockaddr *)&address, &length) < 0) goto fail;

	client = socket(AF_INET, SOCK_STREAM | cloexec | nonblock, 0);
	if (client < 0 ||
	    connect(client, (struct sockaddr *)&address, sizeof address) < 0)
		goto fail;
	server = accept(listener, 0, 0);
	if (server < 0) goto fail;
	if (cloexec && fcntl(server, F_SETFD, FD_CLOEXEC) < 0) goto fail;
	if (nonblock && fcntl(server, F_SETFL, O_NONBLOCK) < 0) goto fail;
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

/* SOCK_DGRAM (2026-09-01): no listener/accept dance -- a datagram
 * socket has no connection to accept (listen.c/accept.c both refuse
 * SOCK_DGRAM outright, see their own comments).  Instead, two ordinary
 * AF_INET/SOCK_DGRAM (UDP) sockets are each bound to an ephemeral
 * loopback port, then connect()'d to *each other's* assigned address --
 * connect() on a datagram socket sets its default destination
 * (connect.html DESCRIPTION) rather than performing a handshake, so
 * this needs no cooperation between the two ends beyond each one
 * knowing the other's address, exactly the same way two independent
 * UDP peers on real loopback addresses would rendezvous.
 *
 * Both ends come from this file's own socket() calls, unlike the
 * SOCK_STREAM path's pair[1] (from accept()) -- so SOCK_CLOEXEC/
 * SOCK_NONBLOCK apply to both directly through socket()'s own flags,
 * and the fcntl() window socketpair_stream() has to accept does not
 * exist here at all. */
static int socketpair_dgram(int cloexec, int nonblock, int pair[2])
{
	struct sockaddr_in addr_a, addr_b;
	socklen_t length;
	int a = -1, b = -1;
	int saved;

	a = socket(AF_INET, SOCK_DGRAM | cloexec | nonblock, 0);
	b = socket(AF_INET, SOCK_DGRAM | cloexec | nonblock, 0);
	if (a < 0 || b < 0) goto fail;

	memset(&addr_a, 0, sizeof addr_a);
	addr_a.sin_family = AF_INET;
	addr_a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(a, (struct sockaddr *)&addr_a, sizeof addr_a) < 0) goto fail;
	length = sizeof addr_a;
	if (getsockname(a, (struct sockaddr *)&addr_a, &length) < 0) goto fail;

	memset(&addr_b, 0, sizeof addr_b);
	addr_b.sin_family = AF_INET;
	addr_b.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(b, (struct sockaddr *)&addr_b, sizeof addr_b) < 0) goto fail;
	length = sizeof addr_b;
	if (getsockname(b, (struct sockaddr *)&addr_b, &length) < 0) goto fail;

	if (connect(a, (struct sockaddr *)&addr_b, sizeof addr_b) < 0) goto fail;
	if (connect(b, (struct sockaddr *)&addr_a, sizeof addr_a) < 0) goto fail;

	pair[0] = a;
	pair[1] = b;
	return 0;

fail:
	saved = errno;
	if (b >= 0) (void)close(b);
	if (a >= 0) (void)close(a);
	errno = saved;
	return -1;
}

int socketpair(int domain, int type, int protocol, int pair[2]) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
{
	int cloexec = type & SOCK_CLOEXEC;
	int nonblock = type & SOCK_NONBLOCK;
	int t = type & ~(SOCK_CLOEXEC | SOCK_NONBLOCK);
	__plat_handle_t native[2];

	if (domain != AF_UNIX) { errno = EAFNOSUPPORT; return -1; }
	if (t != SOCK_STREAM && t != SOCK_DGRAM) { errno = EPROTOTYPE; return -1; }
	if (protocol != 0) { errno = EPROTONOSUPPORT; return -1; }
	if (!pair) { errno = EINVAL; return -1; }

	if (__plat_socketpair(type, native) == 0) {
		int a = install_pair_handle(native[0], t == SOCK_DGRAM, cloexec, nonblock);
		int b;

		if (a < 0) { __plat_close(native[1]); return -1; }
		b = install_pair_handle(native[1], t == SOCK_DGRAM, cloexec, nonblock);
		if (b < 0) { int saved = errno; (void)close(a); errno = saved; return -1; }
		pair[0] = a;
		pair[1] = b;
		return 0;
	}
	if (errno != ENOSYS) return -1; /* a real failure, not "no native primitive" */

	return t == SOCK_DGRAM ? socketpair_dgram(cloexec, nonblock, pair) : socketpair_stream(cloexec, nonblock, pair);
}
