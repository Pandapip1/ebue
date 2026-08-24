/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * shutdown(): https://pubs.opengroup.org/onlinepubs/9699919799/
 * functions/shutdown.html.  SHUT_RD "disables further receive
 * operations", SHUT_WR "disables further send operations", SHUT_RDWR
 * both -- mapped directly onto AFD_DISCONNECT_RECV/SEND
 * (src/internal/afd.h, IOCTL_AFD_DISCONNECT; shared.h's
 * AFD_DISCONNECT_INFO).  Timeout is zeroed: this is a graceful
 * shutdown (no AFD_DISCONNECT_ABORT), not a wait for queued data to
 * drain.
 */
#include <sys/socket.h>
#include <errno.h>
#include "libc.h"
#include "afd.h"

int shutdown(int fd, int how)
{
	struct __fd *f = __fd_get(fd);
	AFD_DISCONNECT_INFO di;
	NTSTATUS st;

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (!(f->pad & AFD_ST_CONNECTED)) { errno = ENOTCONN; return -1; }

	switch (how) {
	case SHUT_RD:   di.DisconnectType = AFD_DISCONNECT_RECV; break;
	case SHUT_WR:   di.DisconnectType = AFD_DISCONNECT_SEND; break;
	case SHUT_RDWR: di.DisconnectType = AFD_DISCONNECT_RECV | AFD_DISCONNECT_SEND; break;
	default: errno = EINVAL; return -1;
	}
	di.Timeout = 0; /* LARGE_INTEGER is a plain LONGLONG here (src/internal/nt.h) */

	st = __afd_ioctl(f->h, IOCTL_AFD_DISCONNECT, &di, sizeof(di), 0, 0, 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}
