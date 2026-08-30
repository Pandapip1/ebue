/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Linux implementation of src/internal/plat_socket.h -- see
 * src/mman/linux/plat_mem.c's own banner for the general discipline this
 * file follows too (raw syscall(2), no host libc, -nostdinc against
 * ntlibc's own headers, aarch64 syscall numbers confirmed against this
 * host).
 *
 * Scope, deliberately narrow: ONLY __plat_sock_recv()/__plat_sock_send(),
 * the genuine POSIX-shaped half of plat_socket.h. Unlike plat_mem.h/
 * plat_fd.h, this interface does not cover socket creation at all --
 * socket()/accept()/bind()/connect()/listen() (src/socket/{socket,accept,
 * bind,connect,listen}.c) call __afd_open()/__afd_ioctl()
 * (src/internal/afd.h) directly, which is raw NT `\Device\Afd` wire-
 * protocol machinery (EA-buffer-encoded open packets, AFD_BIND_DATA,
 * AFD_CONNECT_INFO, ...) with no portable abstraction whatsoever, not
 * even the incomplete one open()'s path-resolution front door has. That
 * is a separate, larger architectural gap -- an entirely different
 * socket-creation abstraction would need designing before any of those
 * five front doors could be ported -- and is deliberately NOT attempted
 * here. This file, and the __plat_sock_* pair it implements, is the
 * entire scope of this backend.
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
#include <sys/socket.h>
#include <errno.h>
#include "plat_socket.h"

/* aarch64 Linux syscall numbers -- see plat_mem.c's banner for why these
 * are hardcoded rather than pulled from a host header. Confirmed against
 * this host's own <sys/syscall.h> (SYS_recvfrom, SYS_sendto) via a
 * throwaway host-gcc oracle program; aarch64 (like every "generic modern
 * ABI" Linux port) has no separate SYS_recv/SYS_send at all -- glibc's
 * own recv()/send() are thin wrappers over these same two syscalls with
 * a NULL address argument, which is exactly what this file does too. */
#define SYS_recvfrom 207
#define SYS_sendto   206

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
static long raw_syscall(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
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
