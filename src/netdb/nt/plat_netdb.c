/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <netdb.h>'s Windows NT backend. The real implementation (a /etc/hosts
 * parser, an /etc/nsswitch.conf-driven dispatcher, and a UDP DNS stub
 * resolver) exists only for native Linux -- see src/netdb/linux/*.c. NT
 * still needs a definition for each declared function, or `make
 * linkcheck` flags a public declaration with no reachable definition.
 *
 * Every entry point here is an honest stand-in: it compiles, links, and
 * reports a real, specified failure (EAI_FAIL, the exact DESCRIPTION
 * wording for "the implementation does not support name resolution on
 * this platform") rather than fabricating an answer. A real NT resolver
 * (DnsQuery_) is future work.
 *
 * getnameinfo() is the one partial exception, and only because its
 * numeric case needs no database at all (see its own comment below). */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

int h_errno;

int getaddrinfo(const char *__restrict node, const char *__restrict service,
                 const struct addrinfo *__restrict hints,
                 struct addrinfo **__restrict res)
{
	(void)node; (void)service; (void)hints;
	*res = NULL;
	return EAI_FAIL;
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

/* getnameinfo(): unlike every other entry point in this file, the
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
