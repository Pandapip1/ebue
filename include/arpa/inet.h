/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * All functions here are implemented in src/socket/inet.c. */
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
const char *inet_ntop(int af, const void *__restrict src,
	char *__restrict dst withtok(writable_span(size)), socklen_t size)
	__attribute__((nonnull(2, 3)));
int inet_pton(int, const char *__restrict, void *__restrict);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
