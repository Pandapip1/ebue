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

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "libc.h"
#include "afd.h"
#include "plat_socket.h"
#include "plat_fd.h"

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
int __afd_open(HANDLE *out, int socktype)
{
	/* The shape is read once and passed to both calls, so that the
	 * buffer's declared size and its contents cannot come from two
	 * different answers.  __afd_open_shape() is cached and constant
	 * for the process, so this is belt-and-braces -- but a size/shape
	 * mismatch here would be a heap overflow, not a wrong packet.
	 * `socktype` (SOCK_STREAM or SOCK_DGRAM) has no such hazard --
	 * __afd_open_ea_size_for() does not depend on it at all (see
	 * afd.h) -- but is threaded through the same way for the same
	 * reason: one value, read once, handed to both calls below. */
	int shape = __afd_open_shape();
	unsigned long ea_size = __afd_open_ea_size_for(shape);
	char *buf;
	UNICODE_STRING devname;
	OBJECT_ATTRIBUTES oa;
	IO_STATUS_BLOCK io;
	HANDLE h = 0;
	NTSTATUS st;

	buf = malloc(ea_size);
	if (!buf) { errno = ENOMEM; return -1; }
	__afd_build_open_ea_for(shape, socktype, buf);

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
ssize_t __plat_sock_recv(__plat_handle_t h, void *buf, size_t len, int flags) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
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

/* socket(): open a fresh AFD endpoint.  __afd_open() (above) already IS
 * the entire body socket()'s front door used to run before this move --
 * this is a thin __plat_handle_t-shaped wrapper over it, not new logic.
 * `type` (SOCK_STREAM or SOCK_DGRAM) is passed straight through -- see
 * plat_socket.h's banner for why this parameter exists. */
int __plat_socket_open(__plat_handle_t *out, int type)
{
	HANDLE h = 0;

	if (__afd_open(&h, type) < 0) return -1;
	*out = h;
	return 0;
}

/* bind(): build the AFD_BIND_DATA request and issue IOCTL_AFD_BIND.
 * Relocated verbatim from src/socket/bind.c's pre-refactor body; only
 * the AFD_SHARE_REUSE/AFD_SHARE_UNIQUE choice, an NT-specific
 * interpretation of the front door's plain `reuseaddr` boolean, is new
 * here (it used to read f->pad & AFD_ST_REUSEADDR itself). */
int __plat_socket_bind(__plat_handle_t h, int reuseaddr, const struct sockaddr *addr, socklen_t len)
{
	AFD_BIND_DATA bd;
	/* IOCTL_AFD_BIND replies with a TDI_ADDRESS_INFO (phnt ntafd.h,
	 * AFD_BIND: "out: TDI_ADDRESS_INFO"), which is 26 bytes for one
	 * AF_INET address -- two bytes *more* than the request's 24-byte
	 * TRANSPORT_ADDRESS payload, so it does not fit back into `bd`.
	 * Spelled as uint32_t[] to get 4-byte alignment without an
	 * alignment attribute. */
	uint32_t reply[(AFD_TDI_ADDRESS_INFO_SIZE_IP + 3) / 4];
	NTSTATUS st;

	if (__afd_build_bind_request(&bd, reuseaddr ? AFD_SHARE_REUSE : AFD_SHARE_UNIQUE, addr, len) < 0)
		return -1;

	/* __afd_bind_request_size(), not sizeof(bd): the request is 26
	 * bytes and sizeof(AFD_BIND_DATA) is 28.  IOCTL_AFD_BIND is
	 * METHOD_NEITHER, so the declared length is what afd.sys bounds
	 * its read of the address by. */
	st = __afd_ioctl(h, IOCTL_AFD_BIND, &bd, (ULONG)__afd_bind_request_size(),
	                 reply, (ULONG)sizeof(reply), 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

/* connect(): build the AFD_CONNECT_INFO request and issue
 * IOCTL_AFD_CONNECT.  Relocated verbatim from src/socket/connect.c's
 * pre-refactor body; the implicit wildcard-bind-first step and the
 * f->pad/f->peer bookkeeping stay in the front door (see plat_socket.h's
 * banner) -- this is only the wire-protocol step. */
int __plat_socket_connect(__plat_handle_t h, const struct sockaddr *addr, socklen_t len)
{
	AFD_CONNECT_INFO ci;
	NTSTATUS st;

	/* Built through src/internal/afd.h's AFD_CONNECT_REQ_OFF_*, not
	 * through AFD_CONNECT_INFO's members: see that header's connect
	 * banner for why the address's offset is pointer-sized, and why
	 * ReactOS's AFD_CONNECT_INFO puts it 12 bytes too early on
	 * x86_64.  `ci` is only the (correctly aligned, large enough)
	 * storage. */
	memset(&ci, 0, sizeof(ci));
	if (__afd_build_connect_request(&ci, addr, len) < 0) return -1;

	/* __afd_connect_request_size(), not sizeof(ci): IOCTL_AFD_CONNECT
	 * is METHOD_NEITHER, so the declared length is what afd.sys
	 * bounds its read of the address by, and sizeof() rounds up. */
	st = __afd_ioctl(h, IOCTL_AFD_CONNECT, &ci, (ULONG)__afd_connect_request_size(), 0, 0, 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

/* listen(): issue IOCTL_AFD_START_LISTEN.  Relocated verbatim from
 * src/socket/listen.c's pre-refactor body; the backlog clamp to
 * SOMAXCONN, the implicit wildcard bind, and the f->pad bookkeeping all
 * stay in the front door -- `backlog` arrives here already clamped. */
int __plat_socket_listen(__plat_handle_t h, unsigned long backlog)
{
	AFD_LISTEN_DATA ld;
	NTSTATUS st;

	ld.UseSAN = 0;
	ld.UseDelayedAcceptance = 0;
	ld.Backlog = (uint32_t)backlog;

	st = __afd_ioctl(h, IOCTL_AFD_START_LISTEN, &ld, sizeof(ld), 0, 0, 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

/* accept(): the two-step AFD sequence, per ReactOS's WSPAccept
 * (dll/win32/msafd/misc/dllmain.c): IOCTL_AFD_WAIT_FOR_LISTEN blocks
 * until a connection is pending and returns its SequenceNumber plus the
 * peer's TDI address; a *new* AFD endpoint is then opened exactly like
 * socket() does, and IOCTL_AFD_ACCEPT -- issued on the *listening*
 * handle, naming the new endpoint's handle in
 * AFD_ACCEPT_DATA.ListenHandle -- binds the pending connection onto it.
 * Relocated verbatim from src/socket/accept.c's pre-refactor body; the
 * f->pad/f->peer bookkeeping and the caller's addr/len truncate-and-copy
 * both stay in the front door (see plat_socket.h's banner) -- `addr`/
 * `len` here are the front door's own full-sized local buffer, which
 * IOCTL_AFD_WAIT_FOR_LISTEN's 16-byte AF_INET reply can never overflow,
 * so no truncation actually happens on this path; the truncating
 * function (__afd_accept_reply_addr() -> __afd_transport_addr_out() ->
 * __afd_addr_to_sockaddr()) is shared with getname.c and always applies
 * the same *len-bounded copy regardless. */
int __plat_socket_accept(__plat_handle_t h, struct sockaddr *addr, socklen_t *len, __plat_handle_t *out)
{
	AFD_RECEIVED_ACCEPT_DATA recvd;
	AFD_ACCEPT_DATA ad;
	HANDLE newh = 0;
	NTSTATUS st;

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
	st = __afd_ioctl(h, IOCTL_AFD_WAIT_FOR_LISTEN, 0, 0, &recvd, sizeof(recvd), 0);
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
	if (__afd_accept_reply_addr(&recvd, addr, len) < 0) {
		errno = ECONNABORTED;
		return -1;
	}

	/* SOCK_STREAM: accept() (src/socket/accept.c) is refused entirely
	 * on a SOCK_DGRAM socket before this backend is ever reached (the
	 * front door's __SOCK_ST_LISTENING check can never be set on one,
	 * since listen.c refuses it too), so the accepted connection here
	 * is always a stream endpoint. */
	if (__afd_open(&newh, SOCK_STREAM) < 0) return -1;

	ad.UseSAN = 0;
	ad.SequenceNumber = recvd.SequenceNumber;
	ad.ListenHandle = newh;

	st = __afd_ioctl(h, IOCTL_AFD_ACCEPT, &ad, sizeof(ad), 0, 0, 0);
	if (!NT_SUCCESS(st)) { __plat_close(newh); return __set_errno_status(st); }

	*out = newh;
	return 0;
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
ssize_t __plat_sock_send(__plat_handle_t h, const void *buf, size_t len, int flags) // NOLINT(bugprone-easily-swappable-parameters) -- positional C interface; parameter names distinguish semantic roles
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
		if (!(flags & MSG_NOSIGNAL)) {
			__sig_lock();
			__raise_internal(SIGPIPE);
			__sig_unlock();
		}
		errno = EPIPE;
		return -1;
	}
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return (ssize_t)io.Information;
}

/* getsockname(): issue IOCTL_AFD_GET_SOCK_NAME and convert the reply.
 * Relocated verbatim from src/socket/getname.c's pre-refactor
 * getsockname() body -- the unbound-socket wildcard-address
 * short-circuit and the __SOCK_ST_BOUND check both stay in the front
 * door (see plat_socket.h's banner); this is only the ioctl step. */
int __plat_socket_getsockname(__plat_handle_t h, struct sockaddr *addr, socklen_t *len)
{
	/* Spelled as uint32_t[] to get 4-byte alignment without an
	 * alignment attribute, same as bind()'s reply buffer -- which is
	 * the same TDI_ADDRESS_INFO, from the same transport. */
	uint32_t reply[(AFD_SOCKNAME_RSP_SIZE + 3) / 4];
	NTSTATUS st;

	/* __afd_sockname_reply_size(), not sizeof(reply): the array is
	 * rounded up to a whole number of uint32_t for alignment, and
	 * declaring those spare bytes to a METHOD_NEITHER driver would
	 * describe two bytes the reply does not. */
	memset(reply, 0, sizeof reply);
	st = __afd_ioctl(h, IOCTL_AFD_GET_SOCK_NAME, 0, 0, reply,
	                 (ULONG)__afd_sockname_reply_size(), 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);

	/* A success that carried no address.  Over the zeroed buffer above
	 * this is what a driver that wrote less than the whole TDI address
	 * looks like; a well-formed reply always passes, so this is a guard
	 * rather than a path.  EINVAL for the same reason getname.c's
	 * NULL-argument case uses it: it is the only code on that page that
	 * fits, and the caller's buffer is left untouched either way. */
	if (__afd_sockname_reply_addr(reply, addr, len) < 0) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

/* shutdown(): build the AFD_DISCONNECT_INFO request and issue
 * IOCTL_AFD_DISCONNECT.  Relocated verbatim from src/socket/shutdown.c's
 * pre-refactor body; the `how` -> SHUT_RD/SHUT_WR/SHUT_RDWR validation
 * and the __SOCK_ST_CONNECTED check both stay in the front door -- `how`
 * arrives here already known to be one of the three valid values. */
int __plat_socket_shutdown(__plat_handle_t h, int how)
{
	AFD_DISCONNECT_INFO di;
	NTSTATUS st;

	switch (how) {
	case SHUT_RD:   di.DisconnectType = AFD_DISCONNECT_RECV; break;
	case SHUT_WR:   di.DisconnectType = AFD_DISCONNECT_SEND; break;
	case SHUT_RDWR: di.DisconnectType = AFD_DISCONNECT_RECV | AFD_DISCONNECT_SEND; break;
	default: errno = EINVAL; return -1;
	}
	di.Timeout = 0; /* LARGE_INTEGER is a plain LONGLONG here (src/internal/nt.h) */

	st = __afd_ioctl(h, IOCTL_AFD_DISCONNECT, &di, sizeof(di), 0, 0, 0);
	if (!NT_SUCCESS(st)) return __set_errno_status(st);
	return 0;
}

/* getsockopt(SO_SNDBUF)/getsockopt(SO_RCVBUF): see plat_socket.h's
 * banner for why this backend answers with a fixed constant instead of
 * a real AFD query.  IOCTL_AFD_GET_INFO's AFD_INFO structure (phnt
 * ntafd.h: AFD_INFO, with AfdInformationClass values including
 * AFD_INFO_SEND_BUFFER_SIZE/AFD_INFO_RECEIVE_BUFFER_SIZE) is the real
 * mechanism ReactOS's WSPGetSockOpt uses for these two options -- but
 * unlike every other ioctl this file issues, its request/reply layout
 * has not been independently cross-checked against a second source the
 * way afd.h's socket-creation banner insists on for everything else
 * here, so it is not used.  8192 is not measured: it is a plausible,
 * round, POSIX-legal (getsockopt.html requires no more than "the size
 * of the buffer" be reported, and this is a value a caller can act on:
 * non-zero, finite, small enough that a fixture deliberately queuing
 * more data than one buffer's worth reliably overflows it) stand-in,
 * chosen so setup_aio() (third_party/ltp's aio_test.h) gets an answer
 * instead of ENOPROTOOPT and can proceed to size its own messages
 * against it -- exactly the role SO_ERROR's fixed "always 0" already
 * plays a few functions up this file's own sibling, sockopt.c. */
#define __NT_SOCKBUF_STANDIN 8192

int __plat_socket_getsndbuf(__plat_handle_t h)
{
	(void)h;
	return __NT_SOCKBUF_STANDIN;
}

int __plat_socket_getrcvbuf(__plat_handle_t h)
{
	(void)h;
	return __NT_SOCKBUF_STANDIN;
}

/* socketpair(): AFD has no native socketpair primitive -- an endpoint
 * is always opened via __afd_open() and separately bound/connected, so
 * there is nothing for this backend to do but say so.  ENOSYS tells
 * src/socket/socketpair.c to fall back to its own bind()/connect()
 * construction (a loopback TCP listener+accept for SOCK_STREAM, a
 * loopback UDP pair for SOCK_DGRAM), exactly as it already did before
 * this function existed. */
int __plat_socketpair(int type, __plat_handle_t out[2])
{
	(void)type;
	(void)out;
	errno = ENOSYS;
	return -1;
}

// NOLINTEND(misc-include-cleaner)
