/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

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
/* src/dst required: src/socket/inet.c's own inet_ntop() casts src to
 * `const unsigned char *b` and subscripts it (b[0]..b[15]) unconditionally
 * whenever af names a supported family, and memcpy(dst, ...) on the
 * success path, with no NULL check of either anywhere in its body.
 * Every real call site (test/posix-socket.c) passes a real address
 * object and a real output buffer, never NULL. */
const char *inet_ntop(int, const void *__restrict, char *__restrict, socklen_t)
    __attribute__((nonnull(2, 3)));
int inet_pton(int, const char *__restrict, void *__restrict);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
