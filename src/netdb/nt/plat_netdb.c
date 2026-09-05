/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <netdb.h>'s Windows NT backend. The real implementation (a /etc/hosts
 * parser, an /etc/nsswitch.conf-driven dispatcher, and a UDP DNS stub
 * resolver) exists only for native Linux -- see src/netdb/linux/. NT
 * still needs a definition for each declared function, or `make
 * linkcheck` flags a public declaration with no reachable definition.
 *
 * Every entry point here is an honest stand-in: it compiles, links, and
 * reports a real, specified failure (EAI_FAIL, the exact DESCRIPTION
 * wording for "the implementation does not support name resolution on
 * this platform") rather than fabricating an answer. A real NT resolver
 * (DnsQuery_) is future work.
 *
 * getaddrinfo() and getnameinfo() are the two partial exceptions, and
 * only because each one's numeric-only case needs no database at all
 * (see each one's own comment below). */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

int h_errno;

/* parse_numeric_port(): a NULL service leaves the port unset (0);
 * anything else must be all-digits and in range. No service-name
 * database exists on this platform (see getservbyname()'s own stub
 * below), so a symbolic service name can never be resolved here --
 * matching src/netdb/linux/addrinfo.c's own parse_service(), which
 * rejects a symbolic name the identical way independent of any
 * platform's own database. */
static int parse_numeric_port(const char *service, unsigned short *port)
{
	const char *p;
	long v;

	if (!service) { *port = 0; return 0; }
	if (!*service) return EAI_SERVICE;
	for (p = service; *p; p++)
		if (!isdigit((unsigned char)*p)) return EAI_SERVICE;
	v = strtol(service, NULL, 10);
	if (v < 0 || v > 65535) return EAI_SERVICE;
	*port = (unsigned short)v;
	return 0;
}

/* getaddrinfo(): freeaddrinfo.html's own DESCRIPTION decides the
 * AI_NUMERICHOST case without any resolver at all: "if the
 * AI_NUMERICHOST flag is specified, then a non-null nodename string
 * shall be a numeric host address string ... Otherwise, an
 * [EAI_NONAME] error shall be returned" -- inet_pton() alone answers
 * that. Every other node is a real name-to-address lookup this
 * platform cannot do yet (see this file's own banner above), so it
 * still gets the honest EAI_FAIL this file has always reported. */
int getaddrinfo(const char *__restrict node, const char *__restrict service,
                 const struct addrinfo *__restrict hints,
                 struct addrinfo **__restrict res)
{
	int family = AF_UNSPEC, socktype = 0, flags = 0;
	struct in_addr addr;
	unsigned short port;
	int rc;
	struct addrinfo *ai;
	struct sockaddr_in *sin;

	*res = NULL;

	if (hints) {
		family = hints->ai_family;
		socktype = hints->ai_socktype;
		flags = hints->ai_flags;
	}

	if (!node || !(flags & AI_NUMERICHOST))
		return EAI_FAIL;
	if (family != AF_UNSPEC && family != AF_INET)
		return EAI_FAMILY;
	if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
		return EAI_SOCKTYPE;
	if (inet_pton(AF_INET, node, &addr) != 1)
		return EAI_NONAME;

	rc = parse_numeric_port(service, &port);
	if (rc) return rc;

	ai = malloc(sizeof *ai);
	if (!ai) return EAI_MEMORY;
	sin = malloc(sizeof *sin);
	if (!sin) { free(ai); return EAI_MEMORY; }

	memset(sin, 0, sizeof *sin);
	sin->sin_family = AF_INET;
	sin->sin_port = htons(port);
	sin->sin_addr = addr;

	memset(ai, 0, sizeof *ai);
	ai->ai_flags = flags;
	ai->ai_family = AF_INET;
	ai->ai_socktype = socktype ? socktype : SOCK_STREAM;
	ai->ai_addrlen = (socklen_t)sizeof *sin;
	ai->ai_addr = (struct sockaddr *)sin;

	*res = ai;
	return 0;
}

struct hostent *gethostbyname(const char *name)
{
	(void)name;
	h_errno = 3 /* NO_RECOVERY: the traditional resolver value for "a
	             * non-recoverable name server error occurred", the
	             * plain historical value every gethostbyname()
	             * implementation uses for this condition. */;
	return NULL;
}

void sethostent(int stayopen) { (void)stayopen; }
struct hostent *gethostent(void) { return NULL; }
void endhostent(void) {}

void setnetent(int stayopen) { (void)stayopen; }
struct netent *getnetent(void) { return NULL; }
void endnetent(void) {}
struct netent *getnetbyname(const char *name) { (void)name; return NULL; }
struct netent *getnetbyaddr(uint32_t net, int type) { (void)net; (void)type; return NULL; }

void setprotoent(int stayopen) { (void)stayopen; }
struct protoent *getprotoent(void) { return NULL; }
void endprotoent(void) {}
struct protoent *getprotobyname(const char *name) { (void)name; return NULL; }
struct protoent *getprotobynumber(int proto) { (void)proto; return NULL; }

void setservent(int stayopen) { (void)stayopen; }
struct servent *getservent(void) { return NULL; }
void endservent(void) {}
struct servent *getservbyname(const char *name, const char *proto)
{
	(void)name; (void)proto;
	return NULL;
}
struct servent *getservbyport(int port, const char *proto)
{
	(void)port; (void)proto;
	return NULL;
}

/* getnameinfo(): like getaddrinfo()'s AI_NUMERICHOST case above, the
 * NI_NUMERICHOST | NI_NUMERICSERV case needs no database this platform
 * lacks -- it is pure number formatting (inet_ntop()) -- so it is
 * answered for real. With no reverse-hosts or services database to
 * consult, node/serv fall back to numeric form (exactly what
 * DESCRIPTION specifies for "the node's name cannot be located") unless
 * NI_NAMEREQD makes that fallback unacceptable, reporting EAI_NONAME
 * instead. */
int getnameinfo(const struct sockaddr *sa, socklen_t salen,
                 char *node, socklen_t nodelen,
                 char *serv, socklen_t servlen, int flags)
{
	const struct sockaddr_in *sin;
	char buf[32];

	if (sa->sa_family != AF_INET) return EAI_FAMILY;
	if (salen < (socklen_t)sizeof(struct sockaddr_in)) return EAI_FAMILY;
	sin = (const struct sockaddr_in *)(const void *)sa;

	if (node && nodelen > 0) {
		if (!(flags & NI_NUMERICHOST) && (flags & NI_NAMEREQD))
			return EAI_NONAME;
		if (!inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf))
			return EAI_OVERFLOW;
		if (strlen(buf) >= nodelen) return EAI_OVERFLOW;
		strcpy(node, buf);
	}

	if (serv && servlen > 0) {
		snprintf(buf, sizeof buf, "%u", (unsigned)ntohs(sin->sin_port));
		if (strlen(buf) >= servlen) return EAI_OVERFLOW;
		strcpy(serv, buf);
	}

	return 0;
}
