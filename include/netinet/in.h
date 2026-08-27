/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <netinet/in.h>: https://pubs.opengroup.org/onlinepubs/9699919799/
 * basedefs/netinet_in.h.html.
 *
 * The pure-C IPv6 text conversion interface needs struct in6_addr even
 * though AF_INET6 sockets remain staged separately (test/networking-
 * audit.md sec 6).  sockaddr_in6 and the IPV6_* socket options stay out
 * of scope until the AFD transport supports them.
 */
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

/* netinet_in.h.html IPPROTO_*: only what AF_INET/SOCK_STREAM needs is
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
