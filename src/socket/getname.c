/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getsockname()/getpeername():
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * getsockname.html and .../getpeername.html.  "retrieve the locally-bound
 * name of the specified socket" / "retrieve the peer address of the
 * specified socket".
 *
 * One file because both calls share descriptor, argument and state
 * validation, even though only getsockname() needs a platform query
 * (__plat_socket_getsockname(), src/internal/plat_socket.h) to answer.
 *
 * Both pages carry the identical address-truncation clause accept.html
 * does: "If the actual length of the address is greater than the length
 * of the supplied sockaddr structure, the stored address shall be
 * truncated" -- and, critically, *address_len still receives the
 * untruncated length, not the number of bytes stored.  getsockname()
 * gets that from __plat_socket_getsockname() itself (each backend's own
 * getsockname(2)/TDI_ADDRESS_INFO reply already truncates the same way);
 * getpeername() applies the same rule to the sockaddr cached when
 * connect()/accept() established the connection.
 *
 * On NT, get-sock-name answers with a TDI_ADDRESS_INFO (a ULONG
 * ActivityCount, then the TRANSPORT_ADDRESS).  The peer address is
 * immutable for a connected stream socket, so retaining the address
 * supplied by connect() or returned to accept() avoids depending on
 * AFD_GET_PEER_NAME, an undocumented ioctl that is not stable across
 * Windows versions -- and lets getpeername() stay one plain struct __fd
 * field access on every backend, with nothing platform-specific to call
 * at all.
 *
 * ERRORS, and the two judgement calls in them:
 *
 *   - [EBADF]/[ENOTSOCK] are the sibling calls' checks, unchanged.
 *   - [EINVAL] for a NULL address or address_len.  Neither page
 *     specifies that case (accept.html explicitly permits a null
 *     address; these two have no such clause, and every argument here
 *     is an out-parameter, so there is nothing left to return), and
 *     EINVAL is the only invalid-argument code on either page's ERRORS
 *     list -- so this reports a caller error without inventing an errno
 *     the clause does not sanction.  It is deliberately NOT the EFAULT
 *     src/socket/sockopt.c uses for the same shape of mistake:
 *     setsockopt.html does list EFAULT, and these two pages do not.
 *   - [ENOTCONN] "The socket is not connected", getpeername only, from
 *     the socket's own AFD_ST_CONNECTED bit rather than from whatever
 *     afd.sys makes of the ioctl on an unconnected endpoint.  Same
 *     reason bind() checks AFD_ST_BOUND itself: the driver's error for
 *     that case (AfdGetPeerName() rejects a NULL FCB->RemoteAddress with
 *     STATUS_INVALID_PARAMETER, which maps to EINVAL, not ENOTCONN) is
 *     not the one POSIX names, and this library knows the answer without
 *     asking.  ReactOS's WSPGetPeerName does the same, checking
 *     SocketConnected before the ioctl.
 *
 * getsockname() on a socket that has never been bound does NOT call
 * __plat_socket_getsockname() at all.  getsockname.html: "If the socket
 * has not been bound to a local name, the value stored in the object
 * pointed to by address is unspecified" -- an unspecified value, not an
 * error.  On NT, asking AFD anyway would turn a conforming success into
 * a nonconforming failure (AfdGetSockName() fails an endpoint with
 * neither an address file nor a connection with
 * STATUS_INVALID_PARAMETER).  The wildcard AF_INET address is returned
 * instead, which is both a legal "unspecified" value and the one every
 * BSD-derived implementation actually produces.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "plat_socket.h"
#include "ownership_stubs.h"

/* The shared preamble: the three checks both calls make, in the order
 * argument validity precedes socket state.  Returns the descriptor, or
 * NULL with errno already set. */
static struct __fd *getname_fd(int fd, struct sockaddr *addr, socklen_t *len)
{
	struct __fd *f = __fd_get(fd);

	if (!f) return 0; /* __fd_get() set EBADF */
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return 0; }
	if (!addr || !len) { errno = EINVAL; return 0; }
	return f;
}

int getsockname(int fd, struct sockaddr *__restrict addr, socklen_t *__restrict len)
{
	struct __fd *f = getname_fd(fd, addr, len);

	if (!f) return -1;

	if (!(f->pad & __SOCK_ST_BOUND)) {
		/* See the banner: unbound is "unspecified", not an error.
		 * An all-zero AF_INET sockaddr_in is the wildcard --
		 * INADDR_ANY, port 0 -- truncate-copied into the caller's
		 * buffer the same way a bound socket's real address is
		 * below, so the truncation contract holds on this path
		 * too. */
		struct sockaddr_in wild;
		socklen_t n;
		socklen_t i;

		memset(&wild, 0, sizeof(wild));
		wild.sin_family = AF_INET;
		n = *len < (socklen_t)sizeof(wild) ? *len : (socklen_t)sizeof(wild);
		for (i = 0; i < n; i++)
			((unsigned char *)addr)[i] = ((const unsigned char *)&wild)[i];
		*len = sizeof(wild);
		return 0;
	}

	return __plat_socket_getsockname(f->h, addr, len);
}

int getpeername(int fd, struct sockaddr *__restrict addr, socklen_t *__restrict len)
{
	struct __fd *f = getname_fd(fd, addr, len);
	socklen_t n;

	if (!f) return -1;
	/* __SOCK_ST_CONNECTED, not afd.h's own AFD_ST_CONNECTED: this file
	 * no longer includes afd.h (getsockname() has been ported off it),
	 * and the two names are numerically identical bits of the same
	 * struct __fd byte -- see plat_socket.h's banner. */
	if (!(f->pad & __SOCK_ST_CONNECTED)) { errno = ENOTCONN; return -1; }
	if (!f->peer_len) { errno = ENOTCONN; return -1; }
	n = *len < f->peer_len ? *len : f->peer_len;
	__ownership_writable_span(addr, n);
	__ownership_readable_span(f->peer, n);
	memcpy(addr, f->peer, n);
	*len = f->peer_len;
	return 0;
}

// NOLINTEND(misc-include-cleaner)
