/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <resolv.h>: not POSIX (historical BSD resolver API). Only dn_expand() is
 * declared; the rest of the historical surface (res_init(), res_query(),
 * `_res`, ns_*()) has no need in this tree, so it stays undeclared rather
 * than fabricated ahead of one.
 *
 * dn_expand() decodes one RFC 1035 sec 4.1.4 compressed domain name from a
 * raw DNS message already in memory: msg/eomorig bound the message,
 * comp_dn points at the name to decode, exp_dn/length bound the output
 * buffer. Pure buffer-walking with no OS dependency, so src/resolv/
 * dn_expand.c has one shared implementation with no nt/linux split.
 */
#ifndef _RESOLV_H
#define _RESOLV_H
#ifdef __cplusplus
extern "C" {
#endif

int dn_expand(const unsigned char *, const unsigned char *,
              const unsigned char *, char *, int);

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
