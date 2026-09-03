/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getnameinfo(): the reverse of getaddrinfo(). The node side does
 * real /etc/hosts reverse resolution via __hosts_lookup_reverse()
 * (hosts.c) before falling back to numeric. One disclosed gap: no PTR
 * DNS query is sent, so a DNS-only name still falls back to numeric,
 * as if NI_NUMERICHOST had been requested -- not wrong (DESCRIPTION
 * allows numeric fallback whenever a name "cannot be located"), just
 * incomplete.
 *
 * The service side asks getservbyport() (services.c), a database that
 * DOES fully exist, so a non-numeric service request is fully
 * resolved, not a fallback.
 *
 * NI_NUMERICHOST | NI_NUMERICSERV needs neither database and is
 * unconditionally exact.
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
