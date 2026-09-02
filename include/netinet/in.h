/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <netinet/in.h>: https://pubs.opengroup.org/onlinepubs/9699919799/
 * basedefs/netinet_in.h.html.
 *
 * The pure-C IPv6 text conversion interface needs struct in6_addr even
 * though AF_INET6 sockets remain staged separately (test/networking-
 * audit.md sec 6).  The IPV6_* socket options stay out of scope until
 * the AFD transport supports them -- those are setsockopt()/getsockopt()
 * verbs an actual IPv6 endpoint would need to answer, and there is no
 * such endpoint (socket(AF_INET6, ...) fails EAFNOSUPPORT,
 * src/socket/socket.c).
 *
 * struct sockaddr_in6 and the IN6_IS_ADDR_ and IN6ADDR_..._INIT surface
 * are NOT staged the same way: this header already declares AF_INET6 itself
 * (<sys/socket.h>, "not implemented; declared only so it compiles" --
 * the precedent this follows) and already gives every socket a
 * sockaddr_storage big enough to hold one, precisely so that a caller
 * can build and inspect an IPv6 address before ever handing it to a
 * function this tree cannot service yet.  A struct and some
 * address-predicate macros carry no linkage of their own to be a "latent
 * link-error bug" the way an undefined function symbol would be (this
 * header's sibling <sys/socket.h> states that rule); sin6_family set to
 * AF_INET6 and handed to socket() still and correctly gets EAFNOSUPPORT.
 * in6addr_any/in6addr_loopback are real data, not stand-ins -- defined
 * in src/socket/inet.c, the same translation unit that already gives
 * the AF_INET6 text-conversion path (inet_ntop()/inet_pton()) its body. */
#ifndef _NETINET_IN_H
#define _NETINET_IN_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <stdint.h>
#include <sys/socket.h>

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

/* netinet_in.h.html: "struct in_addr...in_addr_t s_addr". */
struct in_addr {
	in_addr_t s_addr;
};

struct in6_addr {
	uint8_t s6_addr[16];
};

/* netinet_in.h.html: "struct sockaddr_in...sin_port and sin_addr shall
 * be in network byte order".  sin_zero pads it out to sizeof(struct
 * sockaddr) (see <sys/socket.h>) the way BSD/Winsock's sockaddr_in
 * both do, and matches TDI_ADDRESS_IP's own layout exactly
 * (src/internal/afd.h) -- sin_port/sin_addr/sin_zero here are bit-for-
 * bit what gets copied into/out of a TA_ADDRESS's Address[] bytes. */
struct sockaddr_in {
	sa_family_t sin_family;
	in_port_t sin_port;
	struct in_addr sin_addr;
	unsigned char sin_zero[8];
};

/* netinet_in.h.html: "struct sockaddr_in6...sin6_family...sin6_port...
 * sin6_flowinfo...sin6_addr...sin6_scope_id".  No AF_INET6 endpoint
 * exists to copy this into/out of an AFD wire format (see this header's
 * banner), so there is no TDI layout to match bit-for-bit the way
 * sockaddr_in matches TDI_ADDRESS_IP -- member order and padding here
 * follow the page's own listing order, which is all any caller
 * constructing one by field name can observe. */
struct sockaddr_in6 {
	sa_family_t sin6_family;
	in_port_t sin6_port;
	uint32_t sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t sin6_scope_id;
};

/* netinet_in.h.html: "IN6ADDR_ANY_INIT...IN6ADDR_LOOPBACK_INIT" plus
 * "extern const struct in6_addr in6addr_any; extern const struct
 * in6_addr in6addr_loopback;" -- both defined for real in
 * src/socket/inet.c, not just declared. */
#define IN6ADDR_ANY_INIT      {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}}
#define IN6ADDR_LOOPBACK_INIT {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}}
extern const struct in6_addr in6addr_any;
extern const struct in6_addr in6addr_loopback;

/* netinet_in.h.html: "The following macros...test for special IPv6
 * addresses" -- IN6_IS_ADDR_UNSPECIFIED, _LOOPBACK, _MULTICAST,
 * _LINKLOCAL, _SITELOCAL, _V4MAPPED, _V4COMPATIBLE, and the five
 * _MC_* multicast-scope predicates.  Pure byte tests against
 * s6_addr, same as every text-conversion routine in
 * src/socket/inet.c already does -- no AFD/socket dependency, so
 * nothing here is gated on the transport staging above. */
static __inline int __ntlibc_in6_is_zero(const struct in6_addr *a, int upto)
{
	int i;
	for (i = 0; i < upto; i++) if (a->s6_addr[i]) return 0;
	return 1;
}

#define IN6_IS_ADDR_UNSPECIFIED(a) (__ntlibc_in6_is_zero((a), 16))
#define IN6_IS_ADDR_LOOPBACK(a) \
	(__ntlibc_in6_is_zero((a), 15) && (a)->s6_addr[15] == 1)
#define IN6_IS_ADDR_MULTICAST(a) ((a)->s6_addr[0] == 0xff)
#define IN6_IS_ADDR_LINKLOCAL(a) \
	(((a)->s6_addr[0] == 0xfe) && (((a)->s6_addr[1] & 0xc0) == 0x80))
#define IN6_IS_ADDR_SITELOCAL(a) \
	(((a)->s6_addr[0] == 0xfe) && (((a)->s6_addr[1] & 0xc0) == 0xc0))
#define IN6_IS_ADDR_V4MAPPED(a) \
	(__ntlibc_in6_is_zero((a), 10) && (a)->s6_addr[10] == 0xff && \
	 (a)->s6_addr[11] == 0xff)
#define IN6_IS_ADDR_V4COMPAT(a) \
	(__ntlibc_in6_is_zero((a), 12) && !IN6_IS_ADDR_UNSPECIFIED(a) && \
	 !IN6_IS_ADDR_LOOPBACK(a))
#define IN6_IS_ADDR_MC_NODELOCAL(a) \
	(IN6_IS_ADDR_MULTICAST(a) && (((a)->s6_addr[1] & 0xf) == 0x1))
#define IN6_IS_ADDR_MC_LINKLOCAL(a) \
	(IN6_IS_ADDR_MULTICAST(a) && (((a)->s6_addr[1] & 0xf) == 0x2))
#define IN6_IS_ADDR_MC_SITELOCAL(a) \
	(IN6_IS_ADDR_MULTICAST(a) && (((a)->s6_addr[1] & 0xf) == 0x5))
#define IN6_IS_ADDR_MC_ORGLOCAL(a) \
	(IN6_IS_ADDR_MULTICAST(a) && (((a)->s6_addr[1] & 0xf) == 0x8))
#define IN6_IS_ADDR_MC_GLOBAL(a) \
	(IN6_IS_ADDR_MULTICAST(a) && (((a)->s6_addr[1] & 0xf) == 0xe))

/* netinet_in.h.html IPPROTO_*: only what AF_INET/SOCK_STREAM (TCP) and
 * AF_INET/SOCK_DGRAM (UDP) need -- IPPROTO_TCP and IPPROTO_UDP,
 * respectively (src/socket/socket.c's protocol argument checks) -- is
 * meaningful here; the others are declared for header conformance. */
#define IPPROTO_IP   0
#define IPPROTO_ICMP 1
#define IPPROTO_TCP  6
#define IPPROTO_UDP  17
#define IPPROTO_IPV6 41
#define IPPROTO_RAW  255

/* netinet_in.h.html: "INADDR_ANY...INADDR_BROADCAST". Network byte
 * order is the same in all four bytes for these two, so no htonl()
 * is needed to use them directly in a struct in_addr. */
#define INADDR_ANY       ((in_addr_t)0x00000000)
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)
#define INADDR_LOOPBACK  ((in_addr_t)0x7f000001)
#define INADDR_NONE      ((in_addr_t)0xffffffff)

#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
