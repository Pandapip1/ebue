/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * NT implementation of src/internal/plat_socket.h -- see that header for
 * the contract each function makes and for why this file also carries
 * __afd_open()/__afd_ioctl() (declared in src/internal/afd.h, out of
 * scope to change), not just the __plat_sock_* pair.  Everything here
 * was, until this file existed, inline inside src/socket/afdsupport.c
 * (__afd_open, __afd_ioctl) or src/socket/sendrecv.c (the recv()/send()
 * bodies); nothing changed in substance, only location, and for
 * recv()/send() the addition of a POSIX-shaped return (errno already
 * set, SIGPIPE already raised) in place of a raw NTSTATUS the front
 * door had to interpret itself.
 */
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include "libc.h"
#include "afd.h"
#include "plat_socket.h"

/* Open a fresh \Device\Afd\Endpoint handle carrying the AF_INET/
 * SOCK_STREAM transport ("\Device\Tcp") -- a FILE_FULL_EA_INFORMATION
 * named "AfdOpenPacketXX" whose value is an AFD_OPEN_PACKET naming the
 * transport device.  See src/internal/afd.h's socket-creation banner
 * for the layout and the two sources it is taken from.  Every socket()
 * call and every accept()ed connection needs one of these.
 *
 * The EA buffer itself is built by __afd_build_open_ea_for()
 * (src/socket/afdsupport.c) -- pure byte marshaling, not a syscall, and
 * shared with test/posix-socket-ea.c, which re-parses it on hosts with
 * no working \Device\Afd at all.  Only the actual NtCreateFile and the
 * OBJECT_ATTRIBUTES/UNICODE_STRING it needs live here. */
int __afd_open(HANDLE *out)
{
	/* The shape is read once and passed to both calls, so that the
	 * buffer's declared size and its contents cannot come from two
	 * different answers.  __afd_open_shape() is cached and constant
	 * for the process, so this is belt-and-braces -- but a size/shape
	 * mismatch here would be a heap overflow, not a wrong packet. */
	int shape = __afd_open_shape();
	unsigned long ea_size = __afd_open_ea_size_for(shape);
	char *buf;
	UNICODE_STRING devname;
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK io;
	HANDLE h;
	NTSTATUS st;

	buf = malloc(ea_size);
	if (!buf) { errno = ENOMEM; return -1; }
	__afd_build_open_ea_for(shape, buf);

	/* \Device\Afd\Endpoint (dllmain.c's DevName; confirmed independently
	 * by leftarcode's reverse-engineering series -- see afd.h banner). */
	{
		static const WCHAR endpoint[] = AFD_ENDPOINT_DEVICE;
		devname.Length = (unsigned short)((sizeof(endpoint) / sizeof(WCHAR) - 1) * sizeof(WCHAR));
		devname.MaximumLength = devname.Length + sizeof(WCHAR);
		devname.Buffer = (WCHAR *)endpoint;
	}
	InitializeObjectAttributes(&oa, &devname, OBJ_CASE_INSENSITIVE, 0, 0);

	/* FILE_SYNCHRONOUS_IO_NONALERT: this project's own house style
	 * (src/fcntl/open.c's banner) for every handle read()/write()/the
	 * AFD ioctls below wait on synchronously, rather than ReactOS's
	 * per-call-event scheme (dllmain.c creates a fresh NtCreateEvent
	 * for every ioctl) -- both are legitimate ways to drive AFD's
	 * asynchronous ioctls to completion; this one reuses the
	 * NtWaitForSingleObject(f->h,...)-on-STATUS_PENDING pattern every
	 * other __FD_* type here already relies on (src/unistd/read.c). */
	st = NtCreateFile(&h, GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE, &oa, &io, 0, 0,
	                  FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN_IF,
	                  FILE_SYNCHRONOUS_IO_NONALERT, buf, ea_size);
	free(buf);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	*out = h;
	return 0;
}

/* See afd.h for the contract.  Issue one AFD ioctl and wait for it to
 * finish; STATUS_PENDING is waited out on the handle itself.  Returns
 * the raw NTSTATUS -- six other files under src/socket/ besides
 * accept.c/sendrecv.c call this directly and already interpret specific status
 * values themselves (bind.c's STATUS_ADDRESS_ALREADY_ASSOCIATED,
 * listen.c's, sockopt.c's, ...), so unlike every other function in this
 * file this one cannot collapse its result to errno without breaking
 * those callers; see afd.h's own __afd_ioctl() declaration, which this
 * matches exactly. */
NTSTATUS __afd_ioctl(HANDLE h, ULONG code, void *in, ULONG inlen, void *out, ULONG outlen, IO_STATUS_BLOCK *io_out)
{
	IO_STATUS_BLOCK io;
	NTSTATUS st;

	io.Status = 0; io.Information = 0;
	st = NtDeviceIoControlFile(h, 0, 0, 0, &io, code, in, inlen, out, outlen);
	if (st == STATUS_PENDING) {
		NtWaitForSingleObject(h, 0, 0);
		st = io.Status;
	}
	if (io_out) *io_out = io;
	return st;
}

/* recv(): build the AFD_RECV_INFO request (MSG_OOB/MSG_PEEK -> the
 * matching TDI_RECEIVE_* flag) and issue IOCTL_AFD_RECV.
 *
 * recv.html: "0 ... the peer has performed an orderly shutdown" -- a
 * clean disconnect (STATUS_CONNECTION_DISCONNECTED/STATUS_LOCAL_
 * DISCONNECT/STATUS_REMOTE_DISCONNECT) is folded into a 0-byte return
 * HERE, while the real status is still in hand, not reconstructed by
 * the front door from errno afterward: src/internal/errno.c's generic
 * NTSTATUS->errno mapping sends all three of these to ENOTCONN, which
 * is indistinguishable there from a socket that was never connected at
 * all and must NOT read back as EOF. */
ssize_t __plat_sock_recv(__plat_handle_t h, void *buf, size_t len, int flags)
{
	AFD_WSABUF wb;
	AFD_RECV_INFO ri;
	IO_STATUS_BLOCK io;
	NTSTATUS st;

	wb.len = (unsigned int)len;
	wb.buf = buf;
	ri.BufferArray = &wb;
	ri.BufferCount = 1;
	ri.AfdFlags = 0;
	ri.TdiFlags = 0;
	if (flags & MSG_OOB) ri.TdiFlags |= TDI_RECEIVE_EXPEDITED;
	if (flags & MSG_PEEK) ri.TdiFlags |= TDI_RECEIVE_PEEK;
	if (!ri.TdiFlags) ri.TdiFlags = TDI_RECEIVE_NORMAL;

	st = __afd_ioctl(h, IOCTL_AFD_RECV, &ri, sizeof(ri), 0, 0, &io);
	if (st == STATUS_CONNECTION_DISCONNECTED || st == STATUS_LOCAL_DISCONNECT || st == STATUS_REMOTE_DISCONNECT)
		return 0;
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return (ssize_t)io.Information;
}

/* send(): build the AFD_SEND_INFO request (MSG_OOB -> TDI_SEND_EXPEDITED)
 * and issue IOCTL_AFD_SEND.
 *
 * send.html: EPIPE "the socket is shut down for writing, or ... no
 * longer connected", with SIGPIPE unless MSG_NOSIGNAL.  Raised HERE, not
 * by the front door testing errno==EPIPE afterward: src/internal/
 * errno.c's generic mapping ALSO sends STATUS_CONNECTION_ABORTED and
 * STATUS_REQUEST_ABORTED to the same ECONNABORTED, and STATUS_REQUEST_
 * ABORTED must NOT raise SIGPIPE -- only this call, which still has the
 * real status in hand, can tell a genuinely broken/disconnected/reset
 * connection apart from every other status the generic table happens to
 * map to a similar-looking errno. */
ssize_t __plat_sock_send(__plat_handle_t h, const void *buf, size_t len, int flags)
{
	AFD_WSABUF wb;
	AFD_SEND_INFO si;
	IO_STATUS_BLOCK io;
	NTSTATUS st;

	wb.len = (unsigned int)len;
	wb.buf = (char *)buf;
	si.BufferArray = &wb;
	si.BufferCount = 1;
	si.AfdFlags = 0;
	si.TdiFlags = (flags & MSG_OOB) ? TDI_SEND_EXPEDITED : 0;

	st = __afd_ioctl(h, IOCTL_AFD_SEND, &si, sizeof(si), 0, 0, &io);
	if (st == STATUS_CONNECTION_DISCONNECTED || st == STATUS_LOCAL_DISCONNECT ||
	    st == STATUS_REMOTE_DISCONNECT || st == STATUS_CONNECTION_RESET || st == STATUS_CONNECTION_ABORTED) {
		if (!(flags & MSG_NOSIGNAL)) __raise_internal(SIGPIPE);
		errno = EPIPE;
		return -1;
	}
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return (ssize_t)io.Information;
}
