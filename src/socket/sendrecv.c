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
 * The AFD_RECV_INFO/AFD_SEND_INFO request setup, the ioctl issuance, and
 * every NTSTATUS interpretation (recv.html's "0...the peer has performed
 * an orderly shutdown", send.html's EPIPE/SIGPIPE) live in
 * __plat_sock_recv()/__plat_sock_send() (src/internal/plat_socket.h,
 * src/socket/nt/plat_socket.c) -- the same split src/unistd/read.c and
 * write.c already make against __plat_read()/__plat_write(), and for the
 * same reason: those decisions need the real status in hand, which only
 * the backend still has once the generic NTSTATUS->errno mapping has
 * run.  What is left here is exactly what read()/write() also keep:
 * POSIX validation and this file's own fd-table bookkeeping.
 *
 * sendto()/recvfrom():
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/sendto.html
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/recvfrom.html
 *
 * Both pages describe a strict superset of send()/recv() -- the address
 * argument is what a connectionless (SOCK_DGRAM) transport needs to pick
 * a destination or report a source.  SOCK_DGRAM exists now (<sys/
 * socket.h>'s scope banner, 2026-09-01), but every SOCK_DGRAM socket
 * this project can produce is used connected: socketpair(AF_UNIX,
 * SOCK_DGRAM, ...) hands back an already-connected pair, and nothing
 * calls sendto()/recvfrom() with a real per-datagram destination on an
 * unconnected one (the Open POSIX Test Suite fixture this scope exists
 * for, third_party/ltp's aio_test.h, does not).  sendto.html is
 * explicit for that case: "If the socket is connected, the dest_addr
 * argument shall be ignored" -- so on every connected socket this
 * project can create, stream or datagram, sendto() reduces to send()
 * and recvfrom() reduces to recv() plus reporting the one peer address
 * connect()/accept()/socketpair() already cached (struct __fd's
 * peer/peer_len, see those files).  What sendto()/recvfrom() do NOT
 * reduce to is skipped, not faked: a socket that reaches here
 * unconnected is not a datagram socket waiting for a per-call
 * destination address (that path is not implemented) -- whatever type
 * it is, it was never connect()'d, and gets exactly the ENOTCONN
 * send()/recv() already give it, dest_addr/src_addr notwithstanding.
 */
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include "libc.h"
#include "afd.h"
#include "plat_socket.h"

ssize_t recv(int fd, void *buf withtok(writable_span(len)), size_t len,
	int flags)
{
	struct __fd *f = __fd_get(fd);

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (!(f->pad & AFD_ST_CONNECTED)) { errno = ENOTCONN; return -1; }
	if (len > 0x7fffffff) len = 0x7fffffff;

	return __plat_sock_recv(f->h, buf, len, flags);
}

ssize_t send(int fd, const void *buf withtok(readable_span(len)), size_t len,
	int flags)
{
	struct __fd *f = __fd_get(fd);

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (!(f->pad & AFD_ST_CONNECTED)) { errno = ENOTCONN; return -1; }
	if (len > 0x7fffffff) len = 0x7fffffff;

	return __plat_sock_send(f->h, buf, len, flags);
}

ssize_t sendto(int fd, const void *buf withtok(readable_span(len)), size_t len,
	int flags, const struct sockaddr *dest_addr, socklen_t addrlen)
{
	struct __fd *f = __fd_get(fd);

	(void)dest_addr;
	(void)addrlen;

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (!(f->pad & AFD_ST_CONNECTED)) { errno = ENOTCONN; return -1; }
	if (len > 0x7fffffff) len = 0x7fffffff;

	/* sendto.html: "If the socket is connected, the dest_addr argument
	 * shall be ignored" -- every socket that reaches here is connected,
	 * so dest_addr (validated as far as this project's single address
	 * family goes, by the same convention accept()'s addr/len pair
	 * uses) plays no further part. */
	return __plat_sock_send(f->h, buf, len, flags);
}

ssize_t recvfrom(int fd, void *buf withtok(writable_span(len)), size_t len,
	int flags, struct sockaddr *__restrict src_addr,
	socklen_t *__restrict addrlen)
{
	struct __fd *f = __fd_get(fd);
	ssize_t n;

	if (!f) return -1;
	if (f->type != __FD_SOCKET) { errno = ENOTSOCK; return -1; }
	if (!(f->pad & AFD_ST_CONNECTED)) { errno = ENOTCONN; return -1; }
	if (len > 0x7fffffff) len = 0x7fffffff;

	n = __plat_sock_recv(f->h, buf, len, flags);
	if (n < 0) return -1;

	/* recvfrom.html: "the source address is stored in the sockaddr
	 * structure pointed to by the address argument...If address is a
	 * null pointer, no address is stored."  This project's only
	 * transport is a connected SOCK_STREAM peer, so the "source" of any
	 * datum on it is the one peer connect()/accept() already recorded
	 * (struct __fd's peer/peer_len, see those two files) -- the same
	 * truncate-into-the-caller's-buffer copy accept()'s own addr/len
	 * pair uses. */
	if (src_addr && addrlen) {
		socklen_t n2 = *addrlen < (socklen_t)f->peer_len ?
			*addrlen : (socklen_t)f->peer_len;
		for (socklen_t i = 0; i < n2; i++)
			((unsigned char *)src_addr)[i] =
			    ((const unsigned char *)f->peer)[i];
		*addrlen = f->peer_len;
	}
	return n;
}
