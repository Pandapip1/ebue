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
 * out of scope to change) are a stable AFD-domain entry point six other
 * files under src/socket/ call directly and already interpret a raw
 * NTSTATUS/HANDLE themselves -- moving THEIR actual NtCreateFile/
 * NtDeviceIoControlFile/NtWaitForSingleObject calls means relocating
 * those two functions' bodies into src/socket/nt/plat_socket.c too,
 * verbatim, still under their own afd.h-declared names and NT-shaped
 * signatures, rather than inventing a POSIX-shaped __plat_ twin that
 * six unconverted callers would have no way to reach.  That is the
 * "less clean than mman's/unistd's" divergence the top-level migration
 * report calls out for this subsystem: AFD's own contract is not being
 * redesigned, only relocated.
 *
 * __plat_sock_recv()/__plat_sock_send() below are the genuine POSIX-
 * shaped half of this file -- ssize_t, errno already set on failure,
 * `flags` the plain <sys/socket.h> MSG_* bits -- because recv()/send()
 * have no NT-shaped contract anyone outside this library depends on.
 * Every NT-specific interpretation step lives entirely inside their
 * bodies: MSG_OOB/MSG_PEEK -> TDI_RECEIVE_*, a clean peer shutdown
 * folded into a 0-byte recv() return, and deciding whether a broken
 * connection raises SIGPIPE on send() -- see __plat_sock_send()'s
 * comment on why that decision cannot be made back in the front door.
 */
#ifndef _NTLIBC_PLAT_SOCKET_H
#define _NTLIBC_PLAT_SOCKET_H

#include <stddef.h>
#include <sys/types.h>
#include "plat_handle.h"

ssize_t __plat_sock_recv(__plat_handle_t h, void *buf, size_t len, int flags);
ssize_t __plat_sock_send(__plat_handle_t h, const void *buf, size_t len, int flags);

#endif
