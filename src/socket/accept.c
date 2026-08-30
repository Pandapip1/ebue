/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * accept(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * accept.html.  "extracts the first connection on the queue...creates a
 * new socket...returns a new file descriptor" (DESCRIPTION); "If
 * address is not a null pointer...address_len...will be modified...If
 * the actual length of the address is greater than...the stored
 * address shall be truncated" -- both handled by
 * __afd_accept_reply_addr() (src/socket/afdsupport.c), which also
 * checks that the reply describes an address at all before reading
 * one out of it.  address/
 * address_len are left untouched when address is NULL, matching the
 * DESCRIPTION's "the peer address is not returned" (no clause requires
 * *address_len be touched in that case either).
 *
 * Two-step AFD sequence, per ReactOS's WSPAccept
 * (dll/win32/msafd/misc/dllmain.c): IOCTL_AFD_WAIT_FOR_LISTEN blocks
 * until a connection is pending and returns its SequenceNumber plus the
 * peer's TDI address; a *new* AFD endpoint is then opened exactly like
 * socket() does, and IOCTL_AFD_ACCEPT -- issued on the *listening*
 * handle, naming the new endpoint's handle in AFD_ACCEPT_DATA.ListenHandle
 * -- binds the pending connection onto it.  This project skips the
 * conditional-accept path (lpfnCondition et al in ReactOS's version):
 * out of POSIX's accept() scope entirely, it is a WSAAccept()-only
 * Winsock extension.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "afd.h"
#include "plat_fd.h"

int accept(int fd, struct sockaddr *__restrict addr, socklen_t *__restrict len)
{
	struct __fd *f = __fd_get(fd);
	AFD_RECEIVED_ACCEPT_DATA recvd;
	AFD_ACCEPT_DATA ad;
	__plat_handle_t newh;
	NTSTATUS st;
	int newfd;
	struct sockaddr_in peer;
	socklen_t peerlen = sizeof peer;

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (!(f->pad & AFD_ST_LISTENING)) { errno = EINVAL; return -1; }

	/* Zeroed before the call, not after it, and not left to the
	 * driver.  IOCTL_AFD_WAIT_FOR_LISTEN is METHOD_BUFFERED with an
	 * out-only buffer: the I/O manager copies back exactly
	 * IoStatus.Information bytes from the kernel's SystemBuffer and
	 * leaves everything past that as the caller left it.  Whatever
	 * AfdWaitForListen() does or does not write to its own copy is
	 * therefore not the question -- the question is what is in *this*
	 * buffer past the copy-back, and for an out-only buffer that is
	 * uninitialised stack unless it is put there first.  See the
	 * AFD_ACCEPT_RSP_OFF_* banner in afd.h; this is the same mechanism
	 * that made an aliased IOCTL_AFD_SELECT reply read back as its own
	 * request, with the worse ending, since here the bytes were never
	 * the caller's to begin with. */
	memset(&recvd, 0, sizeof recvd);
	st = __afd_ioctl(f->h, IOCTL_AFD_WAIT_FOR_LISTEN, 0, 0, &recvd, sizeof(recvd), 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	/* Interpreted here, before any endpoint is created, rather than
	 * beside the `if (addr)` at the bottom.  The sequence number and
	 * the peer address arrive in one Information-bounded copy-back, so
	 * a reply that failed to deliver the address is not evidence that
	 * the sequence number beside it is a connection worth accepting --
	 * and failing before __afd_open() keeps accept() all-or-nothing
	 * rather than leaking an endpoint on the way out.
	 *
	 * ECONNABORTED, from accept.html's ERRORS: "A connection has been
	 * aborted."  The connection has already been taken off the listen
	 * queue by the ioctl above and cannot be handed to the caller, so
	 * from the caller's side this pending connection is gone -- which
	 * is exactly the case ECONNABORTED names, and which portable accept
	 * loops already handle by going round again rather than giving up.
	 * EPROTO, the other candidate in that list, is glossed as the
	 * protocol stack not being initialised: it would claim every later
	 * accept() is doomed too, which this does not establish. */
	if (__afd_accept_reply_addr(&recvd, (struct sockaddr *)&peer, &peerlen) < 0) {
		errno = ECONNABORTED;
		return -1;
	}

	if (__afd_open(&newh) < 0) return -1;

	ad.UseSAN = 0;
	ad.SequenceNumber = recvd.SequenceNumber;
	ad.ListenHandle = newh;

	st = __afd_ioctl(f->h, IOCTL_AFD_ACCEPT, &ad, sizeof(ad), 0, 0, 0);
	if (!NT_SUCCESS(st)) { __plat_close(newh); return __set_errno_status(st); }

	newfd = __fd_install(newh, 0, __FD_SOCKET);
	if (newfd < 0) { __plat_close(newh); return -1; }
	__fd_get(newfd)->pad = AFD_ST_BOUND | AFD_ST_CONNECTED;
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
