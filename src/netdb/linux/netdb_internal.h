/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Private glue between this directory's backends (hosts.c, resolv.c)
 * and their two public front doors (addrinfo.c's getaddrinfo(),
 * hostent.c's gethostbyname()). Not installed, not reachable from
 * outside src/netdb/linux/ -- see include/netdb.h for the actual
 * public contract these are built to serve.
 */
#ifndef _NTLIBC_NETDB_INTERNAL_H
#define _NTLIBC_NETDB_INTERNAL_H

#include <netinet/in.h>

/* __hosts_lookup(): /etc/hosts (or its test-fixture override, see
 * src/internal/nss_paths.h), IPv4 A-record-shaped lines only -- a line
 * whose address field contains ':' is a real IPv6 literal and is
 * skipped cleanly rather than mis-parsed (this pass's getaddrinfo()
 * never returns AF_INET6 results; see include/netdb.h's own banner
 * for why). `name` is matched against a line's canonical name (first
 * name after the address) and every alias after it, case-insensitively
 * (hosts(5): names are conventionally matched the same
 * case-insensitive way DNS already treats them). Writes up to
 * maxaddrs matches (network byte order) into addrs and, if canon is
 * non-NULL, the FIRST matching line's own canonical name into canon
 * (truncated to canonsz, always NUL-terminated when canonsz > 0).
 * Returns the number of addresses found, 0 for a clean miss (name not
 * required, maxaddrs may legitimately be 0 to query hosts.c is
 * matched at all without collecting addresses -- see
 * addrinfo.c's own use of that). */
int __hosts_lookup(const char *name, struct in_addr *addrs, int maxaddrs,
                    char *canon, size_t canonsz)
    __attribute__((nonnull(1)));

/* __resolv_query_a(): a real minimal UDP DNS A-record stub resolver --
 * see src/netdb/linux/resolv.c's own banner for the exact wire-format
 * scope (UDP only, no TCP fallback, no DNSSEC) and for why this
 * bypasses the public socket()/connect()/send()/recv() front door
 * entirely (that front door is AF_INET/SOCK_STREAM-only today;
 * see <sys/socket.h>'s own banner -- SOCK_DGRAM is staged, separate
 * work this pass does not depend on or duplicate the intent of).
 * Returns the number of A records found (>= 0, 0 for a clean
 * NXDOMAIN/empty-answer response) or -1 with *reason set on any
 * failure -- no reachable nameserver, a timeout, or a real DNS
 * RCODE this resolver understood well enough to distinguish. */
enum __dns_fail {
	__DNS_NOSERVERS,  /* /etc/resolv.conf named no usable nameserver */
	__DNS_TIMEOUT,    /* every nameserver was tried and none answered */
	__DNS_SERVFAIL,   /* RCODE 2: server failure */
	__DNS_NXDOMAIN,   /* RCODE 3: name does not exist (distinct from a clean empty answer) */
	__DNS_FORMERR,    /* RCODE 1: the server rejected this pass's own query as malformed */
	__DNS_REFUSED,    /* RCODE 5 (and RCODE 4/NOTIMP folded in: this pass sends nothing that should trip either) */
	__DNS_IOERR       /* a real socket()/sendto()/recvfrom() syscall failure other than a timeout */
};
int __resolv_query_a(const char *name, struct in_addr *addrs, int maxaddrs,
                      enum __dns_fail *reason)
    __attribute__((nonnull(1, 4)));

/* __hosts_resolve(): the actual "hosts" NSS database walk shared by
 * addrinfo.c's getaddrinfo() and hostent.c's gethostbyname() --
 * consults __nsswitch_order("hosts", ...) (src/internal/nsswitch.h)
 * and tries each configured service (files, dns) in turn until one
 * produces a positive result or a hard failure; a service that is
 * consulted but yields zero addresses is "keep going", matching
 * nsswitch.conf's own NOTFOUND=continue default (see
 * src/netdb/linux/nsswitch.c's banner for why this project does not
 * implement the `[STATUS=action]` override of that default). Returns
 * the address count (>=0; 0 is a clean overall miss) or -1 with *eai
 * set to an EAI_* code (include/netdb.h) on a hard failure from a
 * service that was actually reached (today, only DNS: see
 * src/netdb/linux/hosts_resolve.c for the __dns_fail -> EAI_* map).
 * canon, if non-NULL, receives the first successful service's own
 * canonical name (truncated to canonsz, always NUL-terminated when
 * canonsz > 0; left untouched on a miss or hard failure). */
int __hosts_resolve(const char *name, struct in_addr *addrs, int maxaddrs,
                     char *canon, size_t canonsz, int *eai)
    __attribute__((nonnull(1, 6)));

#endif
