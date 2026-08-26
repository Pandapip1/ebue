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
 * validation, even though only getsockname() needs an AFD query.
 *
 * Both pages carry the identical address-truncation clause accept.html
 * does: "If the actual length of the address is greater than the length
 * of the supplied sockaddr structure, the stored address shall be
 * truncated" -- and, critically, *address_len still receives the
 * untruncated length, not the number of bytes stored.  getsockname()
 * goes through __afd_transport_addr_out() (src/socket/afdsupport.c);
 * getpeername() applies the same rule to the sockaddr cached when
 * connect()/accept() established the connection.
 *
 * get-sock-name answers with a TDI_ADDRESS_INFO (a ULONG ActivityCount,
 * then the TRANSPORT_ADDRESS).  The peer address is immutable for a
 * connected stream socket, so retaining the address supplied by connect()
 * or returned to accept() avoids depending on AFD_GET_PEER_NAME, an
 * undocumented ioctl that is not stable across Windows versions.
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
 * getsockname() on a socket that has never been bound does NOT go to
 * AFD at all.  getsockname.html: "If the socket has not been bound to a
 * local name, the value stored in the object pointed to by address is
 * unspecified" -- an unspecified value, not an error.  afd.sys would
 * make it one (AfdGetSockName() fails an endpoint with neither an
 * address file nor a connection with STATUS_INVALID_PARAMETER), so
 * asking it would turn a conforming success into a nonconforming
 * failure.  The wildcard AF_INET address is returned instead, which is
 * both a legal "unspecified" value and the one every BSD-derived
 * implementation actually produces.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "afd.h"

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
	/* Spelled as uint32_t[] to get 4-byte alignment without an
	 * alignment attribute, same as bind()'s reply buffer -- which is
	 * the same TDI_ADDRESS_INFO, from the same transport. */
	uint32_t reply[(AFD_SOCKNAME_RSP_SIZE + 3) / 4];
	NTSTATUS st;

	if (!f) return -1;

	if (!(f->pad & AFD_ST_BOUND)) {
		/* See the banner: unbound is "unspecified", not an error.
		 * An all-zero TA_ADDRESS is the wildcard -- INADDR_ANY,
		 * port 0 -- and goes through the same converter so that the
		 * truncation contract holds on this path too. */
		TA_ADDRESS wild;
		memset(&wild, 0, sizeof wild);
		__afd_addr_to_sockaddr(&wild, addr, len);
		return 0;
	}

	/* __afd_sockname_reply_size(), not sizeof(reply): the array is
	 * rounded up to a whole number of uint32_t for alignment, and
	 * declaring those spare bytes to a METHOD_NEITHER driver would
	 * describe two bytes the reply does not. */
	memset(reply, 0, sizeof reply);
	st = __afd_ioctl(f->h, IOCTL_AFD_GET_SOCK_NAME, 0, 0, reply,
	                 (ULONG)__afd_sockname_reply_size(), 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	/* A success that carried no address.  Over the zeroed buffer above
	 * this is what a driver that wrote less than the whole TDI address
	 * looks like; a well-formed reply always passes, so this is a guard
	 * rather than a path.  EINVAL for the same reason the NULL-argument
	 * case uses it: it is the only code on this page that fits, and the
	 * caller's buffer is left untouched either way. */
	if (__afd_sockname_reply_addr(reply, addr, len) < 0) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

int getpeername(int fd, struct sockaddr *__restrict addr, socklen_t *__restrict len)
{
	struct __fd *f = getname_fd(fd, addr, len);
	socklen_t n;

	if (!f) return -1;
	if (!(f->pad & AFD_ST_CONNECTED)) { errno = ENOTCONN; return -1; }
	if (!f->peer_len) { errno = ENOTCONN; return -1; }
	n = *len < f->peer_len ? *len : f->peer_len;
	memcpy(addr, f->peer, n);
	*len = f->peer_len;
	return 0;
}
