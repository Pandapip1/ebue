/* C library internals and platform ABI fields intentionally use the
 * implementation-reserved namespace so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The platform-socket-I/O interface src/socket/sendrecv.c's POSIX-facing
 * recv()/send() call into instead of building an AFD_RECV_INFO/
 * AFD_SEND_INFO request and issuing IOCTL_AFD_RECV/IOCTL_AFD_SEND
 * themselves.  See src/socket/nt/plat_socket.c for the implementation.
 *
 * Unlike plat_mem.h/plat_fd.h this is NOT the only place this
 * subsystem's raw NT syscalls end up: src/socket/afdsupport.c's
 * __afd_open()/__afd_ioctl() (declared in src/internal/afd.h, which is
 * out of scope to change) are a stable AFD-domain entry point files
 * under src/socket/ not yet converted to this interface (getname.c,
 * shutdown.c, sockopt.c) call directly and already interpret a raw
 * NTSTATUS/HANDLE themselves -- moving THEIR actual NtCreateFile/
 * NtDeviceIoControlFile/NtWaitForSingleObject calls means relocating
 * those two functions' bodies into src/socket/nt/plat_socket.c too,
 * verbatim, still under their own afd.h-declared names and NT-shaped
 * signatures, rather than inventing a POSIX-shaped __plat_ twin that
 * those unconverted callers would have no way to reach.  That is the
 * "less clean than mman's/unistd's" divergence the top-level migration
 * report calls out for this subsystem: AFD's own contract is not being
 * redesigned, only relocated.
 *
 * __plat_sock_recv()/__plat_sock_send() are the genuine POSIX-shaped
 * half of this file that predates the rest of it -- ssize_t, errno
 * already set on failure, `flags` the plain <sys/socket.h> MSG_* bits --
 * because recv()/send() have no NT-shaped contract anyone outside this
 * library depends on.  Every NT-specific interpretation step lives
 * entirely inside their bodies: MSG_OOB/MSG_PEEK -> TDI_RECEIVE_*, a
 * clean peer shutdown folded into a 0-byte recv() return, and deciding
 * whether a broken connection raises SIGPIPE on send() -- see
 * __plat_sock_send()'s comment on why that decision cannot be made back
 * in the front door.
 *
 * __plat_socket_{open,bind,connect,listen,accept}() below are the
 * socket-CREATION half: what src/socket/{socket,bind,connect,listen,
 * accept}.c used to do by calling __afd_open()/__afd_ioctl() directly
 * (raw \Device\Afd wire protocol -- EA-buffer-encoded open packets,
 * AFD_BIND_DATA, AFD_CONNECT_INFO, the two-step AFD_WAIT_FOR_LISTEN +
 * fresh __afd_open() + AFD_ACCEPT accept() dance -- see
 * src/internal/afd.h).  Each front door's own POSIX-socket-state-
 * machine bookkeeping (the __SOCK_ST_* bits stashed in struct __fd's
 * `pad` byte, the ENOTSOCK/already-bound/already-connected checks, the
 * backlog clamp, the implicit wildcard bind before connect()/listen(),
 * accept()'s post-success addr/len truncate-and-copy) is NOT part of
 * this interface and stays in the front door, unchanged -- exactly like
 * __plat_fd.h's fd-table bookkeeping stays in src/unistd/{close,read,
 * write,...}.c.  Only the actual wire-protocol/syscall step moves.
 *
 * bind()'s `reuseaddr` is a plain boolean: which AFD_SHARE_* enum value
 * that becomes (AFD_SHARE_REUSE vs AFD_SHARE_UNIQUE) is a genuine NT
 * interpretation step and lives entirely inside __plat_socket_bind()'s
 * NT body -- the front door has no AFD-shaped concept to hand over.
 *
 * accept()'s NT backend is two real AFD steps (wait for a pending
 * connection, then open a fresh endpoint and bind the connection onto
 * it) collapsed into the one portable call below; the NT backend keeps
 * doing both steps internally, the Linux backend does the POSIX-native
 * one-step accept4(2).  addr/len follow accept(2)'s own convention
 * (silently truncate-if-too-small) so the front door can pass its own
 * full-sized local buffer through unchanged and do its usual
 * truncate-into-the-caller's-buffer copy afterward. */
#ifndef _NTLIBC_PLAT_SOCKET_H
#define _NTLIBC_PLAT_SOCKET_H

#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "plat_handle.h"

ssize_t __plat_sock_recv(__plat_handle_t h, void *buf, size_t len, int flags);
ssize_t __plat_sock_send(__plat_handle_t h, const void *buf, size_t len, int flags);

/* Per-socket connection-state bits stashed in struct __fd's `pad` byte
 * for __FD_SOCKET descriptors -- this library's own bookkeeping, not
 * anything either backend's wire protocol actually defines.  Numerically
 * identical to src/internal/afd.h's own AFD_ST_* (kept there, unchanged,
 * for the AFD-specific front doors -- getname.c/shutdown.c/sockopt.c/
 * sendrecv.c -- not yet converted to this interface): both name bits of
 * the very same struct __fd byte for the very same socket, so the two
 * definitions must never be allowed to drift apart. */
#define __SOCK_ST_BOUND     0x01
#define __SOCK_ST_LISTENING 0x02
#define __SOCK_ST_CONNECTED 0x04
#define __SOCK_ST_REUSEADDR 0x08

/* out required: both real implementations (linux/plat_socket.c,
 * nt/plat_socket.c) write `*out = box(...)`/`*out = ...` unconditionally
 * on their success path, with no NULL check of out itself anywhere.
 * socket.c's one real call site always passes `&h`, the address of its
 * own local, never NULL. */
int __plat_socket_open(__plat_handle_t *out) __attribute__((nonnull(1)));
int __plat_socket_bind(__plat_handle_t h, int reuseaddr, const struct sockaddr *addr, socklen_t len);
int __plat_socket_connect(__plat_handle_t h, const struct sockaddr *addr, socklen_t len);
int __plat_socket_listen(__plat_handle_t h, unsigned long backlog);
/* out required, same shape as __plat_socket_open()'s own: both real
 * implementations write `*out = ...` unconditionally on the success
 * path. accept.c's one real call site always passes `&newh`, never
 * NULL. addr/len are NOT required here: neither backend dereferences
 * them directly in C (both only forward the raw pointer/length into a
 * syscall -- SYS_accept4's own arguments on Linux, the AFD wait/accept
 * request on NT), so there is nothing at this level for the attribute
 * to describe, matching this tree's own "forwarding-only, not marked"
 * precedent (e.g. posix_spawn_file_actions.c's add*() functions and
 * fa). */
int __plat_socket_accept(__plat_handle_t h, struct sockaddr *addr, socklen_t *len, __plat_handle_t *out)
    __attribute__((nonnull(4)));

#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
