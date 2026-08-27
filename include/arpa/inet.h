/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <arpa/inet.h>: https://pubs.opengroup.org/onlinepubs/9699919799/
 * basedefs/arpa_inet.h.html for the header contents; each function is
 * cited individually in src/socket/inet.c, which defines all of these.
 *
 * inet_ntoa() is declared even though nothing here calls it internally:
 * it needs no OS support (pure formatting, like the rest of this
 * header) and is part of the mandatory header content, so leaving it
 * out would be exactly the "declared elsewhere, not here" trap this
 * project avoids -- except here there is a body for it (src/socket/
 * inet.c), so it *is* declared.  The AF_INET6 text types are present as
 * well; only the AF_INET6/UDP/AF_UNIX socket transports remain staged.
 */
#ifndef _ARPA_INET_H
#define _ARPA_INET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <stdint.h>
#include <netinet/in.h>

uint32_t htonl(uint32_t);
uint16_t htons(uint16_t);
uint32_t ntohl(uint32_t);
uint16_t ntohs(uint16_t);

in_addr_t inet_addr(const char *);
char *inet_ntoa(struct in_addr);
const char *inet_ntop(int, const void *__restrict, char *__restrict, socklen_t);
int inet_pton(int, const char *__restrict, void *__restrict);

#ifdef __cplusplus
}
#endif
#endif
