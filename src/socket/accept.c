/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * accept(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * accept.html.  "extracts the first connection on the queue...creates a
 * new socket...returns a new file descriptor" (DESCRIPTION); "If
 * address is not a null pointer...address_len...will be modified...If
 * the actual length of the address is greater than...the stored
 * address shall be truncated" -- both handled here, via a local
 * full-sized `peer`/`peerlen` buffer __plat_socket_accept() (src/
 * internal/plat_socket.h) fills that can never itself be truncated (an
 * AF_INET address always fits in a struct sockaddr_in), followed by this
 * front door's own truncate-into-the-caller's-buffer copy below -- the
 * same two-step shape the pre-portable version had, just with the
 * backend call replacing an inline two-step AFD sequence.  address/
 * address_len are left untouched when address is NULL, matching the
 * DESCRIPTION's "the peer address is not returned" (no clause requires
 * *address_len be touched in that case either).
 *
 * The NT backend's real sequence is two AFD steps (ReactOS's WSPAccept,
 * dll/win32/msafd/misc/dllmain.c: IOCTL_AFD_WAIT_FOR_LISTEN blocks until
 * a connection is pending and returns its SequenceNumber plus the peer's
 * TDI address; a *new* AFD endpoint is then opened exactly like socket()
 * does, and IOCTL_AFD_ACCEPT binds the pending connection onto it) --
 * collapsed into the one portable call below; see src/socket/nt/
 * plat_socket.c's __plat_socket_accept() for where that two-step dance,
 * and the ECONNABORTED-on-a-reply-with-no-address handling that goes
 * with it, now lives.  This project skips the conditional-accept path
 * (lpfnCondition et al in ReactOS's version): out of POSIX's accept()
 * scope entirely, it is a WSAAccept()-only Winsock extension.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "plat_socket.h"
#include "plat_fd.h"

int accept(int fd, struct sockaddr *__restrict addr, socklen_t *__restrict len)
{
	struct __fd *f = __fd_get(fd);
	__plat_handle_t newh;
	int newfd;
	struct sockaddr_in peer;
	socklen_t peerlen = sizeof peer;

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (!(f->pad & __SOCK_ST_LISTENING)) { errno = EINVAL; return -1; }

	if (__plat_socket_accept(f->h, (struct sockaddr *)&peer, &peerlen, &newh) < 0) return -1;

	newfd = __fd_install(newh, 0, __FD_SOCKET);
	if (newfd < 0) { __plat_close(newh); return -1; }
	__fd_get(newfd)->pad = __SOCK_ST_BOUND | __SOCK_ST_CONNECTED;
	memcpy(__fd_get(newfd)->peer, &peer, sizeof peer);
	__fd_get(newfd)->peer_len = sizeof peer;

	/* Converted only here, on the success path, so that a failure
	 * between the check above and this point still leaves the caller's
	 * address buffer untouched -- the behaviour every earlier revision
	 * of this function had.  The reply was validated before any of that
	 * happened, so this call cannot fail. */
	if (addr) {
		socklen_t n = *len < (socklen_t)sizeof peer ? *len : (socklen_t)sizeof peer;
		memcpy(addr, &peer, n);
		*len = sizeof peer;
	}

	return newfd;
}
