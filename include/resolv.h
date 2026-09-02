/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <resolv.h>: not POSIX (historical BSD resolver API, glibc/musl both
 * carry it under _DEFAULT_SOURCE/_BSD_SOURCE, exactly the guard
 * third_party/libc-test's own dn_expand-empty.c/dn_expand-ptr-0.c
 * define before including this header). Only dn_expand() is declared
 * here -- the rest of the historical <resolv.h> surface (res_init(),
 * res_query(), the global `_res` state struct, ns_*() message-parsing
 * helpers) has no corpus test in this tree needing it, so it stays
 * undeclared rather than fabricated ahead of a real need.
 *
 * dn_expand() decodes one RFC 1035 sec 4.1.4 compressed domain name
 * out of a raw DNS message already in memory -- msg/eomorig bound the
 * whole message, comp_dn points at the name to decode (which may
 * itself be anywhere in [msg, eomorig)), exp_dn/length bound the
 * output buffer. This is pure buffer-walking logic with no OS
 * dependency (same message format on every platform this project
 * targets), so src/resolv/dn_expand.c gives it one shared
 * implementation with no nt/linux split, the same shape as
 * src/misc/mntent.c (see that file's own banner for the precedent).
 * src/netdb/linux/resolv.c's own skip_name() already walks this exact
 * wire format for its stub resolver's internal use; dn_expand() is
 * the public, general-purpose front door for the same kind of walk,
 * not a duplicate of that internal helper.
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
