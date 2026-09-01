/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * __hosts_resolve(): the "hosts" NSS database walk shared by
 * getaddrinfo() (src/netdb/linux/addrinfo.c) and gethostbyname()
 * (src/netdb/linux/hostent.c) -- see netdb_internal.h's own comment
 * for the exact contract, and src/netdb/linux/nsswitch.c's banner for
 * why service order is consulted but per-service [STATUS=action]
 * qualifiers are not.
 */
#include <netdb.h>
#include "nsswitch.h"
#include "netdb_internal.h"

/* __dns_fail -> EAI_*. __DNS_IOERR deliberately does NOT map to
 * EAI_SYSTEM: this resolver's own raw syscalls (src/netdb/linux/
 * resolv.c) never route their failures through this library's global
 * errno (they run below and outside the public socket() front door
 * entirely), so EAI_SYSTEM's own contract -- "the error code is in
 * errno" -- would be a lie here. EAI_FAIL ("a non-recoverable error
 * occurred") is the honest bucket for every hard DNS-side failure
 * this resolver cannot usefully distinguish further for the caller. */
static int dns_fail_to_eai(enum __dns_fail f)
{
	switch (f) {
	case __DNS_TIMEOUT:
	case __DNS_SERVFAIL:
		return EAI_AGAIN;
	default:
		return EAI_FAIL;
	}
}

int __hosts_resolve(const char *name, struct in_addr *addrs, int maxaddrs,
                     char *canon, size_t canonsz, int *eai)
{
	enum __nss_service order[4];
	int norder = __nsswitch_order("hosts", order, 4);
	int i;

	for (i = 0; i < norder; i++) {
		if (order[i] == __NSS_SVC_FILES) {
			int n = __hosts_lookup(name, addrs, maxaddrs, canon, canonsz);
			if (n > 0) return n;
			/* 0: a clean miss in /etc/hosts -- try the next
			 * configured service, per nsswitch.conf's own
			 * NOTFOUND=continue default. */
		} else { /* __NSS_SVC_DNS */
			enum __dns_fail reason;
			int n = __resolv_query_a(name, addrs, maxaddrs, &reason);
			if (n > 0) {
				if (canon && canonsz > 0) {
					size_t l;
					for (l = 0; name[l] && l + 1 < canonsz; l++) canon[l] = name[l];
					canon[l] = '\0';
				}
				return n;
			}
			if (n < 0) { *eai = dns_fail_to_eai(reason); return -1; }
			/* n == 0: NXDOMAIN or an empty NOERROR answer --
			 * also a clean miss, try the next service. */
		}
	}
	return 0;
}
