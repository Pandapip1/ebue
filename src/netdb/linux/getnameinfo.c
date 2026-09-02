/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getnameinfo(): https://pubs.opengroup.org/onlinepubs/9699919799/
 * functions/getnameinfo.html, the reverse of getaddrinfo(). The node
 * side below does real, honest /etc/hosts reverse resolution via
 * __hosts_lookup_reverse() (src/netdb/linux/hosts.c) before ever
 * falling back to numeric. One real, disclosed gap remains: no PTR DNS
 * query is sent, so a name only in DNS (not in the local hosts file)
 * still falls back to its numeric form, exactly as if NI_NUMERICHOST
 * had been requested -- not a wrong answer (DESCRIPTION explicitly
 * allows a numeric fallback whenever the name "cannot be located", and
 * only requires an error when NI_NAMEREQD says fallback itself is
 * unacceptable), just an incomplete resolver.
 *
 * The service side asks services.c's getservbyport() for a name (a
 * database that DOES fully exist, unlike DNS-only hostnames), so a
 * non-numeric service request is fully resolved, not merely a numeric
 * fallback.
 *
 * NI_NUMERICHOST | NI_NUMERICSERV -- test/posix-netdb.c's own
 * posix_netdb_getnameinfo_numeric fence -- needs neither database at
 * all and is unconditionally exact.
 */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include "netdb_internal.h"

int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                 char *node, socklen_t nodelen,
                 char *serv, socklen_t servlen, int flags)
{
	const struct sockaddr_in *sin;
	char hostbuf[NI_MAXHOST];
	char servbuf[NI_MAXSERV];
	unsigned short port;

	if (sa->sa_family != AF_INET) return EAI_FAMILY;
	if (salen < (socklen_t)sizeof(struct sockaddr_in)) return EAI_FAMILY;
	sin = (const struct sockaddr_in *)(const void *)sa;
	port = ntohs(sin->sin_port);

	if (node && nodelen > 0) {
		int have_name = 0;

		if (!(flags & NI_NUMERICHOST))
			have_name = __hosts_lookup_reverse(&sin->sin_addr, hostbuf, sizeof hostbuf);
		if (!have_name) {
			if ((flags & NI_NAMEREQD) && !(flags & NI_NUMERICHOST))
				return EAI_NONAME;
			if (!inet_ntop(AF_INET, &sin->sin_addr, hostbuf, sizeof hostbuf))
				return EAI_OVERFLOW;
		}
		if (strlen(hostbuf) >= nodelen) return EAI_OVERFLOW;
		strcpy(node, hostbuf);
	}

	if (serv && servlen > 0) {
		if (flags & NI_NUMERICSERV) {
			snprintf(servbuf, sizeof servbuf, "%u", (unsigned)port);
		} else {
			const char *proto = (flags & NI_DGRAM) ? "udp" : "tcp";
			struct servent *se = getservbyport((int)sin->sin_port, proto);

			if (se) {
				size_t n = strlen(se->s_name);
				if (n >= sizeof servbuf) n = sizeof servbuf - 1;
				memcpy(servbuf, se->s_name, n);
				servbuf[n] = '\0';
			} else {
				snprintf(servbuf, sizeof servbuf, "%u", (unsigned)port);
			}
		}
		if (strlen(servbuf) >= servlen) return EAI_OVERFLOW;
		strcpy(serv, servbuf);
	}

	return 0;
}
