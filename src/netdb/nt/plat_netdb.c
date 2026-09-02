/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <netdb.h>'s Windows NT backend. The real implementation (a /etc/hosts
 * parser, an /etc/nsswitch.conf-driven dispatcher, and a UDP DNS stub
 * resolver) exists only for native Linux -- see src/netdb/linux/*.c.
 * include/netdb.h declares these functions on every platform, so NT
 * still needs a definition for each, or `make linkcheck` flags a public
 * declaration with no reachable definition (worse than no declaration:
 * a caller gets a confusing link error instead of a clear "not on this
 * platform").
 *
 * Every entry point here is an honest stand-in: it compiles, links, and
 * reports a real, specified failure -- EAI_FAIL ("a non-recoverable
 * failure in name resolution occurred", the exact DESCRIPTION wording
 * for "the implementation does not support name resolution on this
 * platform") -- rather than fabricating an answer or silently
 * succeeding with garbage. A real NT resolver (DnsQuery_, or the Linux
 * backend's /etc/hosts + resolv.conf approach ported to NT's own path
 * conventions) is future work.
 *
 * getnameinfo() and the four enumerable database families (host/
 * network/protocol/service) follow the same honest-stand-in shape: NT
 * has no default per-machine database for any of them, and this
 * backend does not parse %SystemRoot%\system32\drivers\etc\* or any
 * other real NT path. getnameinfo() is the one partial exception, and
 * only because its numeric case needs no database at all (see its own
 * comment below). */
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
	h_errno = 3 /* NO_RECOVERY, the traditional resolver value for
	             * "a non-recoverable name server error occurred" --
	             * <netdb.h> does not itself define the h_errno
	             * constants (they are outside this edition of POSIX,
	             * per this file's own banner in the Linux backend),
	             * so this is the plain historical value every other
	             * gethostbyname() implementation uses for the same
	             * condition, not a value this tree invented. */;
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
 * lacks -- it is pure number formatting (inet_ntop(), already real and
 * platform-shared, see src/socket/inet.c) -- so it is answered for
 * real rather than joining the honest-failure list above. Once either
 * flag is absent, this backend has no reverse-hosts database and no
 * services database to consult (see this file's own banner), so the
 * node/serv side falls back to its numeric form -- exactly what
 * DESCRIPTION already specifies for "the node's name cannot be located"
 * -- unless NI_NAMEREQD says that fallback itself is unacceptable, in
 * which case this honestly reports EAI_NONAME instead of a name it does
 * not have. */
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
