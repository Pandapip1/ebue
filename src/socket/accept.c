/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * accept(): https://pubs.opengroup.org/onlinepubs/9699919799/functions/
 * accept.html.  "extracts the first connection on the queue...creates a
 * new socket...returns a new file descriptor" (DESCRIPTION); "If
 * address is not a null pointer...address_len...will be modified...If
 * the actual length of the address is greater than...the stored
 * address shall be truncated" -- both handled by
 * __afd_addr_to_sockaddr() (src/socket/afdsupport.c).  address/
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
#include <errno.h>
#include "libc.h"
#include "afd.h"

int accept(int fd, struct sockaddr *__restrict addr, socklen_t *__restrict len)
{
	struct __fd *f = __fd_get(fd);
	AFD_RECEIVED_ACCEPT_DATA recvd;
	AFD_ACCEPT_DATA ad;
	HANDLE newh;
	NTSTATUS st;
	int newfd;

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (!(f->pad & AFD_ST_LISTENING)) { errno = EINVAL; return -1; }

	st = __afd_ioctl(f->h, IOCTL_AFD_WAIT_FOR_LISTEN, 0, 0, &recvd, sizeof(recvd), 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	if (__afd_open(&newh) < 0) return -1;

	ad.UseSAN = 0;
	ad.SequenceNumber = recvd.SequenceNumber;
	ad.ListenHandle = newh;

	st = __afd_ioctl(f->h, IOCTL_AFD_ACCEPT, &ad, sizeof(ad), 0, 0, 0);
	if (!NT_SUCCESS(st)) { NtClose(newh); return __set_errno_status(st); }

	newfd = __fd_install(newh, 0, __FD_SOCKET);
	if (newfd < 0) { NtClose(newh); return -1; }
	__fd_get(newfd)->pad = AFD_ST_BOUND | AFD_ST_CONNECTED;

	if (addr) __afd_addr_to_sockaddr(&recvd.Address.Address[0], addr, len);

	return newfd;
}
