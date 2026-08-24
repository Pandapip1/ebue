/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * send()/recv():
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/send.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/recv.html
 *
 * Both go through IOCTL_AFD_SEND/IOCTL_AFD_RECV (AFD_SEND_INFO/
 * AFD_RECV_INFO, ReactOS's WSPSend/WSPRecv -- dll/win32/msafd/misc/
 * sndrcv.c) rather than plain NtWriteFile/NtReadFile on the socket
 * handle: test/networking-audit.md sec 2 flags this specifically as
 * unverified ("this audit could not establish...whether a bare
 * NtReadFile/NtWriteFile...behaves correctly end-to-end") and
 * recommends the ioctl form as the safe choice, which this follows.
 * src/unistd/read.c and write.c gain a thin __FD_SOCKET branch that
 * calls these, rather than duplicating the AFD_RECV_INFO/AFD_SEND_INFO
 * setup there -- see those two files.
 *
 * recv.html: "0...the peer has performed an orderly shutdown" -- a
 * clean disconnect (STATUS_CONNECTION_DISCONNECTED/
 * STATUS_LOCAL_DISCONNECT/STATUS_REMOTE_DISCONNECT, ntstatus.h) is
 * folded into a 0-byte return the same way src/unistd/read.c already
 * treats STATUS_PIPE_BROKEN/STATUS_PIPE_DISCONNECTED as EOF rather than
 * an error.
 *
 * send.html: EPIPE "the socket is shut down for writing, or...no longer
 * connected", with SIGPIPE unless MSG_NOSIGNAL -- mirrors
 * src/unistd/write.c's existing STATUS_PIPE_BROKEN/DISCONNECTED/CLOSING
 * handling exactly, just against the AFD-flavoured disconnect statuses
 * instead of the pipe ones.
 */
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>
#include "libc.h"
#include "afd.h"

ssize_t recv(int fd, void *buf, size_t len, int flags)
{
	struct __fd *f = __fd_get(fd);
	AFD_WSABUF wb;
	AFD_RECV_INFO ri;
	IO_STATUS_BLOCK io;
	NTSTATUS st;

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (!(f->pad & AFD_ST_CONNECTED)) { errno = ENOTCONN; return -1; }
	if (len > 0x7fffffff) len = 0x7fffffff;

	wb.len = (unsigned int)len;
	wb.buf = buf;
	ri.BufferArray = &wb;
	ri.BufferCount = 1;
	ri.AfdFlags = 0;
	ri.TdiFlags = 0;
	if (flags & MSG_OOB) ri.TdiFlags |= TDI_RECEIVE_EXPEDITED;
	if (flags & MSG_PEEK) ri.TdiFlags |= TDI_RECEIVE_PEEK;
	if (!ri.TdiFlags) ri.TdiFlags = TDI_RECEIVE_NORMAL;

	st = __afd_ioctl(f->h, IOCTL_AFD_RECV, &ri, sizeof(ri), 0, 0, &io);
	if (st == STATUS_CONNECTION_DISCONNECTED || st == STATUS_LOCAL_DISCONNECT || st == STATUS_REMOTE_DISCONNECT)
		return 0;
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return (ssize_t)io.Information;
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
	struct __fd *f = __fd_get(fd);
	AFD_WSABUF wb;
	AFD_SEND_INFO si;
	IO_STATUS_BLOCK io;
	NTSTATUS st;

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (!(f->pad & AFD_ST_CONNECTED)) { errno = ENOTCONN; return -1; }
	if (len > 0x7fffffff) len = 0x7fffffff;

	wb.len = (unsigned int)len;
	wb.buf = (char *)buf;
	si.BufferArray = &wb;
	si.BufferCount = 1;
	si.AfdFlags = 0;
	si.TdiFlags = (flags & MSG_OOB) ? TDI_SEND_EXPEDITED : 0;

	st = __afd_ioctl(f->h, IOCTL_AFD_SEND, &si, sizeof(si), 0, 0, &io);
	if (st == STATUS_CONNECTION_DISCONNECTED || st == STATUS_LOCAL_DISCONNECT ||
	    st == STATUS_REMOTE_DISCONNECT || st == STATUS_CONNECTION_RESET || st == STATUS_CONNECTION_ABORTED) {
		if (!(flags & MSG_NOSIGNAL)) __raise_internal(SIGPIPE);
		errno = EPIPE;
		return -1;
	}
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return (ssize_t)io.Information;
}
