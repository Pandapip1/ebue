/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_socket.h -- see
 * src/mman/linux/plat_mem.c's own banner for the general discipline this
 * file follows too (raw syscall(2), no host libc, -nostdinc against
 * ntlibc's own headers, aarch64 syscall numbers confirmed against this
 * host).
 *
 * __plat_sock_recv()/__plat_sock_send() are the genuine POSIX-shaped
 * half of plat_socket.h that predates the rest of it.  Below them,
 * __plat_socket_{open,bind,connect,listen,accept}() are the socket-
 * CREATION half: real socket(2)/bind(2)/connect(2)/listen(2)/accept4(2)
 * syscalls, genuinely simple compared to the NT backend's \Device\Afd
 * wire protocol -- no EA-buffer-encoded open packets, no AFD_BIND_DATA/
 * AFD_CONNECT_INFO, no two-step wait-for-listen-then-accept dance, just
 * the kernel's own socket ABI, which ntlibc's own AF_INET (2), SOCK_
 * STREAM (1), IPPROTO_TCP (6) and struct sockaddr_in layout (family at
 * +0, port at +2, addr at +4, 16 bytes total) all already match exactly
 * -- confirmed against this host's real <sys/socket.h>/<netinet/in.h>
 * via a throwaway host-gcc oracle program, the same way plat_fcntl.c's
 * O_DIRECTORY/O_NOFOLLOW/O_DIRECT mismatch was caught, so nothing here
 * is assumed rather than verified. SOL_SOCKET/SO_REUSEADDR are the one
 * exception -- ntlibc's own <sys/socket.h> gives them a private encoding
 * (0xffff/0x0004) that does NOT match the real kernel ABI (1/2,
 * confirmed the same way), so __plat_socket_bind()'s reuseaddr flag is
 * translated to the kernel's own SOL_SOCKET/SO_REUSEADDR via an explicit
 * setsockopt(2) before bind(2) -- exactly the LX_MSG_* translation
 * pattern to_linux_flags() below already uses for send()/recv().
 *
 * __plat_handle_t encoding: matches src/unistd/linux/plat_fd.c's own
 * boxed-fd scheme exactly (a real fd is stored as fd+1, so
 * __PLAT_HANDLE_NULL/0 never collides with the valid fd 0) -- a Linux
 * socket fd lives in the very same descriptor space as a Linux file fd,
 * so there is no reason for this backend to invent a second encoding;
 * whichever code installed the handle (a ported socket()/accept() some
 * day, or this pilot's own test harness standing in for them today) is
 * expected to box it the same way.
 *
 * recv()/send()'s NT backend has real interpretation work to do here
 * (STATUS_*_DISCONNECTED -> a 0-byte recv() return, EPIPE/SIGPIPE
 * disambiguation on send() -- see src/socket/nt/plat_socket.c's own
 * comments) because NT's AFD ioctls hand back a raw NTSTATUS with no
 * native POSIX shape at all. None of that exists here: a Linux
 * recvfrom(2) already returns 0 on a clean peer shutdown natively (no
 * separate "which status means orderly close" table needed), and a
 * Linux sendto(2) against a broken connection already returns -EPIPE
 * *and* already has the kernel raise real SIGPIPE against this process
 * as an ordinary side effect (unless MSG_NOSIGNAL, which the kernel also
 * already honors) -- exactly the same story src/unistd/linux/plat_fd.c's
 * __plat_write() comment already tells for write()/broken pipes: the
 * signal is real and kernel-delivered, not something ntlibc must
 * synthesize itself the way the NT backend's __raise_internal() does.
 */

/* This translation unit implements ntlibc's freestanding -nostdinc
 * public-header contract; transitive ABI declarations are intentional,
 * so hosted include ownership and unused-include advice do not apply. */
// NOLINTBEGIN(misc-include-cleaner)
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include "plat_socket.h"

/* aarch64 Linux syscall numbers -- see plat_mem.c's banner for why these
 * are hardcoded rather than pulled from a host header. Confirmed against
 * this host's own <sys/syscall.h> (SYS_recvfrom, SYS_sendto) via a
 * throwaway host-gcc oracle program; aarch64 (like every "generic modern
 * ABI" Linux port) has no separate SYS_recv/SYS_send at all -- glibc's
 * own recv()/send() are thin wrappers over these same two syscalls with
 * a NULL address argument, which is exactly what this file does too.
 *
 * SYS_getsockname/SYS_shutdown (added for __plat_socket_getsockname()/
 * __plat_socket_shutdown() below) were confirmed the same way, and slot
 * exactly where every other socket-family syscall above and below them
 * already does in both tables: aarch64's generic syscall table runs
 * socket=198, socketpair=199, bind=200, listen=201, accept=202,
 * connect=203, getsockname=204, getpeername=205, sendto=206,
 * recvfrom=207, setsockopt=208, getsockopt=209, shutdown=210 back to
 * back -- this file's own already-verified SYS_socket/SYS_bind/
 * SYS_listen/SYS_connect/SYS_sendto/SYS_recvfrom/SYS_setsockopt values
 * are exactly that run with getsockname/shutdown left out, so the two
 * new numbers are read off the same table rather than guessed.  x86_64's
 * unistd_64.h table is the same story: socket=41, connect=42, accept=43,
 * sendto=44, recvfrom=45, sendmsg=46, recvmsg=47, shutdown=48, bind=49,
 * listen=50, getsockname=51, getpeername=52, socketpair=53,
 * setsockopt=54, getsockopt=55 -- again contiguous with, and confirming,
 * this file's existing x86_64 numbers below. */
#if defined(__aarch64__)
#define SYS_recvfrom     207
#define SYS_sendto       206
#define SYS_socket       198
#define SYS_bind         200
#define SYS_listen       201
#define SYS_connect      203
#define SYS_getsockname  204
#define SYS_setsockopt   208
#define SYS_shutdown     210
#define SYS_accept4      242
#elif defined(__x86_64__)
#define SYS_recvfrom     45
#define SYS_sendto       44
#define SYS_socket       41
#define SYS_shutdown     48
#define SYS_bind         49
#define SYS_listen       50
#define SYS_connect      42
#define SYS_getsockname  51
#define SYS_setsockopt   54
#define SYS_accept4      288
#else
#error "plat_socket.c: unsupported architecture"
#endif

/* A minimal 6-argument raw syscall: `svc #0` directly, no host libc in
 * the call path at all. NOT `extern long syscall(long, ...)`: that
 * symbol is satisfied by the HOST's real glibc at link time (this
 * build is -nostdinc, not -nostdlib -- only compiling avoids the host
 * headers, the final link step still pulls in host libc), and glibc's
 * syscall() performs its own error translation: on failure it returns
 * exactly -1 and sets glibc's OWN errno (a different memory location
 * than ntlibc's own errno global, src/internal/errno.c) to the real
 * code -- it does NOT hand back the raw kernel -errno in [-4095,-1]
 * this file's is_sys_error()/`errno = (int)-ret` translation requires.
 * Confirmed both by inspecting the linked pilot binary (nm -D shows an
 * undefined `syscall@GLIBC_*`, resolved by ld-linux at runtime) and
 * independently by src/thread/linux/plat_thread.c's own port, which
 * hit the identical bug and is this fix's model. aarch64's syscall
 * calling convention: x8 = syscall number, x0..x5 = up to 6 arguments,
 * result (or -errno in [-4095,-1]) in x0. */
#if defined(__aarch64__)
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6) // NOLINT(bugprone-easily-swappable-parameters) -- raw syscall ABI slots are positional and semantically distinct
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a1;
	register long x1 __asm__("x1") = a2;
	register long x2 __asm__("x2") = a3;
	register long x3 __asm__("x3") = a4;
	register long x4 __asm__("x4") = a5;
	register long x5 __asm__("x5") = a6;
	__asm__ volatile("svc #0"
		: "+r"(x0)
		: "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
		: "memory", "cc");
	return x0;
}
#elif defined(__x86_64__)
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
	long ret;
	register long r10 __asm__("r10") = a4;
	register long r8  __asm__("r8")  = a5;
	register long r9  __asm__("r9")  = a6;
	__asm__ volatile("syscall"
	                 : "=a"(ret)
	                 : "a"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
	                 : "rcx", "r11", "memory");
	return ret;
}
#endif

/* A raw Linux syscall returns the result on success, or -errno (as an
 * unsigned value in [-4095, -1]) on failure -- see plat_mem.c's own
 * comment for the full explanation of this convention. */
static int is_sys_error(long ret)
{
	return (unsigned long)ret >= (unsigned long)-4095L;
}

/* Matches src/unistd/linux/plat_fd.c's unbox() exactly -- see this
 * file's own banner for why the encoding is shared rather than
 * reinvented. */
static int unbox(__plat_handle_t h)
{
	return (int)((long)h - 1);
}

/* The other direction, for the two calls that hand back a brand new fd
 * (__plat_socket_open()'s socket(2), __plat_socket_accept()'s
 * accept4(2)) rather than operating on an already-boxed one -- matches
 * src/unistd/linux/plat_fd.c's __plat_dup() (`*out = (__plat_handle_t)
 * (newfd + 1);`), which does the same boxing inline rather than through
 * a named helper; named here because __plat_socket_open() and
 * __plat_socket_accept() both need it. */
static __plat_handle_t box(int fd)
{
	return (__plat_handle_t)(long)(fd + 1);
}

/* ntlibc's own <sys/socket.h> SOL_SOCKET/SO_REUSEADDR are, like MSG_*
 * above, a private encoding (0xffff/0x0004) that does not match the
 * real Linux kernel ABI (1/2, confirmed against this host's own
 * <sys/socket.h> via a throwaway oracle program) -- so bind()'s
 * reuseaddr flag is translated to the kernel's own numbers explicitly,
 * never passed through. */
#define LX_SOL_SOCKET   1
#define LX_SO_REUSEADDR 2

/* ntlibc's own <sys/socket.h> MSG_* bits are a private encoding, not a
 * copy of any host ABI (see that header's own comment: they exist "so
 * e.g. socket(AF_INET6, ...) compiles", not because their numeric values
 * are load-bearing anywhere outside this library). MSG_OOB/MSG_PEEK/
 * MSG_DONTROUTE/MSG_NOSIGNAL happen to already match the Linux kernel's
 * own recvfrom(2)/sendto(2) flag bits (confirmed against this host's
 * <sys/socket.h>), but MSG_TRUNC/MSG_CTRUNC/MSG_EOR/MSG_WAITALL do NOT --
 * so, exactly like the NT backend translates MSG_OOB into
 * TDI_RECEIVE_EXPEDITED explicitly rather than assuming any bit
 * position, this backend translates each flag explicitly into the
 * Linux kernel's own bit rather than passing ntlibc's raw `flags` value
 * straight through. */
#define LX_MSG_OOB       0x0001
#define LX_MSG_PEEK      0x0002
#define LX_MSG_DONTROUTE 0x0004
#define LX_MSG_CTRUNC    0x0008
#define LX_MSG_TRUNC     0x0020
#define LX_MSG_EOR       0x0080
#define LX_MSG_WAITALL   0x0100
#define LX_MSG_NOSIGNAL  0x4000

static int to_linux_flags(int flags)
{
	int out = 0;
	if (flags & MSG_OOB)       out |= LX_MSG_OOB;
	if (flags & MSG_PEEK)      out |= LX_MSG_PEEK;
	if (flags & MSG_DONTROUTE) out |= LX_MSG_DONTROUTE;
	if (flags & MSG_CTRUNC)    out |= LX_MSG_CTRUNC;
	if (flags & MSG_TRUNC)     out |= LX_MSG_TRUNC;
	if (flags & MSG_EOR)       out |= LX_MSG_EOR;
	if (flags & MSG_WAITALL)   out |= LX_MSG_WAITALL;
	if (flags & MSG_NOSIGNAL)  out |= LX_MSG_NOSIGNAL;
	return out;
}

/* recv(): recvfrom(2) with a NULL address -- this project's only
 * connected-socket use (src/socket/sendrecv.c's recv() already refused
 * anything not AFD_ST_CONNECTED before calling here), so there is no
 * peer address to capture. recv.html's "0 ... the peer has performed an
 * orderly shutdown" is already exactly what a Linux recvfrom() returns
 * on a clean peer close -- no separate disconnect-status table needed,
 * unlike the NT backend (see this file's banner). */
ssize_t __plat_sock_recv(__plat_handle_t h, void *buf, size_t len, int flags)
{
	long ret = raw_syscall(SYS_recvfrom, (long)unbox(h), (long)buf, (long)len,
	                       (long)to_linux_flags(flags), 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (ssize_t)ret;
}

/* send(): sendto(2) with a NULL address, for the same "already connected"
 * reason __plat_sock_recv() has none either. send.html's EPIPE/SIGPIPE
 * pair is already exactly what a Linux sendto() against a broken
 * connection does on its own (real kernel-delivered SIGPIPE, suppressed
 * by MSG_NOSIGNAL, both already honored by the kernel before this
 * syscall even returns) -- nothing left for this backend to raise
 * itself, unlike the NT backend's explicit __raise_internal(SIGPIPE)
 * (see this file's banner). */
ssize_t __plat_sock_send(__plat_handle_t h, const void *buf, size_t len, int flags)
{
	long ret = raw_syscall(SYS_sendto, (long)unbox(h), (long)buf, (long)len,
	                       (long)to_linux_flags(flags), 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return (ssize_t)ret;
}

/* socket(): a real socket(2), AF_INET/SOCK_STREAM/IPPROTO_TCP always --
 * this project's only supported triple (src/socket/socket.c's front door
 * has already rejected anything else before calling here), and the same
 * fixed pair the NT backend's __afd_open() bakes into its own open
 * packet rather than taking as parameters. */
int __plat_socket_open(__plat_handle_t *out)
{
	long fd = raw_syscall(SYS_socket, (long)AF_INET, (long)SOCK_STREAM, (long)IPPROTO_TCP, 0L, 0L, 0L);
	if (is_sys_error(fd)) { errno = (int)-fd; return -1; }
	*out = box((int)fd);
	return 0;
}

/* bind(): SO_REUSEADDR (if requested) via a real setsockopt(2), then a
 * real bind(2) -- unlike the NT backend, which folds the equivalent
 * choice (AFD_SHARE_REUSE vs AFD_SHARE_UNIQUE) into the bind request
 * itself, Linux's socket ABI has no such share-mode field on bind(2): the
 * option must already be set on the fd before bind(2) runs. addr/len are
 * passed straight through -- ntlibc's struct sockaddr_in is already
 * byte-identical to the kernel's (see this file's banner), so no
 * marshaling is needed the way AFD's TDI_ADDRESS_IP translation needs. */
int __plat_socket_bind(__plat_handle_t h, int reuseaddr, const struct sockaddr *addr, socklen_t len)
{
	int fd = unbox(h);
	long ret;

	if (reuseaddr) {
		int one = 1;
		ret = raw_syscall(SYS_setsockopt, (long)fd, (long)LX_SOL_SOCKET, (long)LX_SO_REUSEADDR,
		                  (long)&one, (long)sizeof(one), 0L);
		if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	}

	ret = raw_syscall(SYS_bind, (long)fd, (long)addr, (long)len, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* connect(): a real connect(2).  The front door's implicit
 * wildcard-bind-before-connect step (POSIX's own "if the initiating
 * socket is not bound, it will be bound to an address selected by the
 * transport layer") stays in src/socket/connect.c, unchanged -- by the
 * time this function runs, the socket is always already bound, so there
 * is nothing left for connect(2) itself to do implicitly. */
int __plat_socket_connect(__plat_handle_t h, const struct sockaddr *addr, socklen_t len)
{
	long ret = raw_syscall(SYS_connect, (long)unbox(h), (long)addr, (long)len, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* listen(): a real listen(2).  `backlog` arrives here already clamped to
 * SOMAXCONN by the front door. */
int __plat_socket_listen(__plat_handle_t h, unsigned long backlog)
{
	long ret = raw_syscall(SYS_listen, (long)unbox(h), (long)backlog, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* accept(): a real accept4(2) (flags 0 -- this project has no
 * SOCK_NONBLOCK/SOCK_CLOEXEC wiring for sockets yet, same as the NT
 * backend's blocking-only accept()).  One syscall does the whole job
 * natively: no separate wait-for-a-pending-connection step, and no
 * separately opened endpoint to bind the connection onto afterward --
 * Linux's accept4(2) already returns a live, connected fd for the new
 * peer in one call, unlike AFD's two-step WAIT_FOR_LISTEN + fresh
 * __afd_open() + ACCEPT dance.  addr/len are the front door's own
 * full-sized local buffer, passed straight through: accept4(2) already
 * implements accept.html's own truncate-if-too-small contract natively
 * (SUSv4 accept(): "If the actual length of the address is greater than
 * the length of the supplied sockaddr structure, the stored address
 * shall be truncated"), so there is nothing left for this backend to
 * interpret. */
int __plat_socket_accept(__plat_handle_t h, struct sockaddr *addr, socklen_t *len, __plat_handle_t *out)
{
	long fd = raw_syscall(SYS_accept4, (long)unbox(h), (long)addr, (long)len, 0L, 0L, 0L);
	if (is_sys_error(fd)) { errno = (int)-fd; return -1; }
	*out = box((int)fd);
	return 0;
}

/* getsockname(): a real getsockname(2).  addr/len are passed straight
 * through -- a real getsockname(2) already implements the truncate-if-
 * too-small contract this project's own front door needs
 * (getsockname.html's clause, identical to accept.html's), and ntlibc's
 * struct sockaddr_in is already byte-identical to the kernel's (see this
 * file's banner), so there is no marshaling step the way AFD's
 * TDI_ADDRESS_INFO reply needs on the NT backend. The front door's own
 * unbound-socket wildcard-address short-circuit (getsockname.c) means
 * this is only ever reached for an already-bound socket, so there is no
 * "unbound" case here to special-case either. */
int __plat_socket_getsockname(__plat_handle_t h, struct sockaddr *addr, socklen_t *len)
{
	long ret = raw_syscall(SYS_getsockname, (long)unbox(h), (long)addr, (long)len, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

/* shutdown(): a real shutdown(2).  `how` is passed straight through --
 * Linux's SHUT_RD/SHUT_WR/SHUT_RDWR are the same POSIX-standard values
 * (0/1/2) ntlibc's own <sys/socket.h> already uses, unlike the NT
 * backend's AFD_DISCONNECT_RECV/SEND bitmask translation, so there is no
 * mapping step here at all. */
int __plat_socket_shutdown(__plat_handle_t h, int how)
{
	long ret = raw_syscall(SYS_shutdown, (long)unbox(h), (long)how, 0L, 0L, 0L, 0L);
	if (is_sys_error(ret)) { errno = (int)-ret; return -1; }
	return 0;
}

// NOLINTEND(misc-include-cleaner)
