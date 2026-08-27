/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <sys/socket.h>: https://pubs.opengroup.org/onlinepubs/9699919799/
 * basedefs/sys_socket.h.html for the header contents, plus the
 * per-function pages this project's function bodies (src/socket/ (every .c there))
 * cite individually.
 *
 * Scope, per test/networking-audit.md and this header's own task:
 * AF_INET with SOCK_STREAM only.  UDP (sendto/recvfrom/sendmsg/
 * recvmsg/SOCK_DGRAM's actual use), AF_INET6, general AF_UNIX pathname
 * sockets, and sockatmark() are all
 * staged for later work (networking-audit.md sec 6, stages 4-6) and are
 * deliberately *not declared* here -- this project's own standing rule
 * (see test/posix-sysmisc.c's file banner) is that a declared-but-
 * undefined symbol is a latent link-error bug, not a lesser form of
 * "not implemented yet", so nothing is declared before it has a body.
 * The SOCK_, AF_, SO_ and MSG_ constants are all defined regardless
 * (free, and needed so e.g. `socket(AF_INET6, ...)` compiles and fails
 * at runtime with EAFNOSUPPORT rather than at compile time).
 *
 * getsockname()/getpeername() were on that deferred list until
 * src/socket/getname.c gave them bodies, and the reason they came off
 * it early is worth stating: nothing in the staged plan actually
 * blocked them.  Address introspection needs no new address family, no
 * new socket type and no new transport -- only two more ioctls on the
 * endpoint socket() already opens, reusing the very TDI-address
 * interpretation accept() was already doing.  The list they were on is
 * a list of work that needs a stage, not a list of things this scope
 * cannot express.
 */
#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#define __NEED_size_t
#define __NEED_ssize_t
#define __NEED_socklen_t
#define __NEED_sa_family_t

#include <bits/alltypes.h>

/* sys_socket.h.html: "The sockaddr structure...". sa_data is sized 14 so
 * that sizeof(struct sockaddr) matches sockaddr_in's own size (this
 * project's only address family) -- POSIX leaves the exact size
 * unspecified beyond "large enough". */
struct sockaddr {
	sa_family_t sa_family;
	char sa_data[14];
};

/* Large enough to hold a sockaddr_in (this project's only address
 * family); POSIX only requires it be as large as the largest sockaddr
 * variant the implementation supports and suitably aligned. */
struct sockaddr_storage {
	sa_family_t ss_family;
	char __ss_pad[26];
};

struct linger {
	int l_onoff;
	int l_linger;
};

/* AF_ and PF_ (sys_socket.h.html DESCRIPTION): numerically these must
 * match the values already baked into src/internal/afd.h's
 * TDI_ADDRESS_TYPE_IP, since AF_INET is written directly into AFD's
 * wire-format TA_ADDRESS.AddressType by src/socket/afdsupport.c -- NT's
 * own AF_INET/SOCK_STREAM/IPPROTO_TCP numbering matches BSD's (2/1/6),
 * confirmed by ReactOS's WSPSocket switch (dllmain.c) using the same
 * values this header does. */
#define AF_UNSPEC 0
#define AF_UNIX   1
#define AF_INET   2
#define AF_INET6  23  /* not implemented; declared only so it compiles */
#define PF_UNSPEC AF_UNSPEC
#define PF_UNIX   AF_UNIX
#define PF_INET   AF_INET
#define PF_INET6  AF_INET6

#define SOCK_STREAM    1
#define SOCK_DGRAM     2  /* not implemented; declared only so it compiles */
#define SOCK_RAW       3  /* not implemented */
#define SOCK_SEQPACKET 5  /* not implemented */

#define SOL_SOCKET 0xffff

#define SO_REUSEADDR  0x0004
#define SO_KEEPALIVE  0x0008
#define SO_DONTROUTE  0x0010
#define SO_BROADCAST  0x0020
#define SO_LINGER     0x0080
#define SO_OOBINLINE  0x0100
#define SO_SNDBUF     0x1001
#define SO_RCVBUF     0x1002
#define SO_SNDLOWAT   0x1003
#define SO_RCVLOWAT   0x1004
#define SO_SNDTIMEO   0x1005
#define SO_RCVTIMEO   0x1006
#define SO_ERROR      0x1007
#define SO_TYPE       0x1008
#define SO_ACCEPTCONN 0x1009
#define SO_DEBUG      0x0001

#define MSG_OOB       0x01
#define MSG_PEEK      0x02
#define MSG_DONTROUTE 0x04
#define MSG_EOR       0x08
#define MSG_TRUNC     0x10
#define MSG_CTRUNC    0x20
#define MSG_WAITALL   0x40
#define MSG_NOSIGNAL  0x4000

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

/* listen.html: "implementations may impose a limit on backlog and
 * silently reduce the specified value"; SOMAXCONN is that limit.  AFD's
 * own AFD_LISTEN_DATA.Backlog is a plain ULONG with no documented cap
 * of its own, so this is this project's chosen ceiling, not one AFD
 * imposes -- src/socket/listen.c clamps to it. */
#define SOMAXCONN 128

int socket(int, int, int);
int socketpair(int, int, int, int [2]);
int bind(int, const struct sockaddr *, socklen_t);
int listen(int, int);
int accept(int, struct sockaddr *__restrict, socklen_t *__restrict);
int connect(int, const struct sockaddr *, socklen_t);
int getsockname(int, struct sockaddr *__restrict, socklen_t *__restrict);
int getpeername(int, struct sockaddr *__restrict, socklen_t *__restrict);
ssize_t send(int, const void *, size_t, int);
ssize_t recv(int, void *, size_t, int);
int shutdown(int, int);
int setsockopt(int, int, int, const void *, socklen_t);
int getsockopt(int, int, int, void *__restrict, socklen_t *__restrict);

#ifdef __cplusplus
}
#endif
#endif
