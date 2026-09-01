/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * getaddrinfo()/freeaddrinfo()/gai_strerror(): https://pubs.opengroup.
 * org/onlinepubs/9699919799/functions/freeaddrinfo.html. See
 * include/netdb.h's own banner for this pass's overall scope
 * (AF_INET only, no service-name database, "files"/"dns" NSS order
 * from a real /etc/nsswitch.conf).
 *
 * Each returned struct addrinfo, its ai_addr, and (on the first node
 * only, when AI_CANONNAME was requested and a name was actually
 * resolved) its ai_canonname are three separate malloc()s, freed by
 * freeaddrinfo() walking ai_next -- the plain, obvious ownership shape
 * DESCRIPTION already documents ("along with any additional storage
 * associated with those structures").
 */
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "netdb_internal.h"

#define MAX_RESULT_ADDRS 32

static int all_digits(const char *s)
{
	if (!*s) return 0;
	for (; *s; s++) if (!isdigit((unsigned char)*s)) return 0;
	return 1;
}

/* parse_service(): NULL -> port 0 (RETURN VALUE / DESCRIPTION: a NULL
 * service leaves the port unset). A decimal string -> that port,
 * range-checked. Anything else -> EAI_SERVICE: this implementation
 * has no service-name database (getservbyname() is not built, see
 * include/netdb.h's own banner), so a symbolic service name can never
 * be resolved here, independent of AI_NUMERICSERV. */
static int parse_service(const char *service, unsigned short *port)
{
	long v;

	if (!service) { *port = 0; return 0; }
	if (!all_digits(service)) return EAI_SERVICE;
	v = strtol(service, NULL, 10);
	if (v < 0 || v > 65535) return EAI_SERVICE;
	*port = (unsigned short)v;
	return 0;
}

static struct addrinfo *make_node(int socktype, int protocol,
                                   struct in_addr addr, unsigned short port)
{
	struct addrinfo *ai = malloc(sizeof *ai);
	struct sockaddr_in *sin;

	if (!ai) return NULL;
	sin = malloc(sizeof *sin);
	if (!sin) { free(ai); return NULL; }

	memset(sin, 0, sizeof *sin);
	sin->sin_family = AF_INET;
	sin->sin_port = htons(port);
	sin->sin_addr = addr;

	memset(ai, 0, sizeof *ai);
	ai->ai_family = AF_INET;
	ai->ai_socktype = socktype;
	ai->ai_protocol = protocol;
	ai->ai_addrlen = (socklen_t)sizeof *sin;
	ai->ai_addr = (struct sockaddr *)sin;
	return ai;
}

void freeaddrinfo(struct addrinfo *res)
{
	while (res) {
		struct addrinfo *next = res->ai_next;
		free(res->ai_addr);
		free(res->ai_canonname);
		free(res);
		res = next;
	}
}

int getaddrinfo(const char *__restrict node, const char *__restrict service,
                 const struct addrinfo *__restrict hints,
                 struct addrinfo **__restrict res)
{
	int family = AF_UNSPEC, socktype = 0, protocol = 0, flags = 0;
	unsigned short port;
	int rc;
	struct in_addr addrs[MAX_RESULT_ADDRS];
	int naddrs = 0;
	char canon[256];
	int have_canon = 0;
	struct addrinfo *head = NULL, *tail = NULL;
	int i;

	*res = NULL;

	if (hints) {
		family = hints->ai_family;
		socktype = hints->ai_socktype;
		protocol = hints->ai_protocol;
		flags = hints->ai_flags;
	}
	if (family != AF_UNSPEC && family != AF_INET) return EAI_FAMILY;
	if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
		return EAI_SOCKTYPE;
	(void)protocol; /* not independently validated: derived below from socktype */

	if (!node && !service) return EAI_NONAME;

	rc = parse_service(service, &port);
	if (rc) return rc;

	if (!node) {
		struct in_addr a;
		a.s_addr = (flags & AI_PASSIVE) ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
		addrs[0] = a;
		naddrs = 1;
	} else if (inet_pton(AF_INET, node, &addrs[0]) == 1) {
		naddrs = 1;
		if (flags & AI_CANONNAME) {
			size_t l = strlen(node);
			if (l >= sizeof canon) l = sizeof canon - 1;
			memcpy(canon, node, l);
			canon[l] = '\0';
			have_canon = 1;
		}
	} else if (flags & AI_NUMERICHOST) {
		return EAI_NONAME;
	} else {
		int eai = 0;
		naddrs = __hosts_resolve(node, addrs, MAX_RESULT_ADDRS,
		                          (flags & AI_CANONNAME) ? canon : NULL,
		                          (flags & AI_CANONNAME) ? sizeof canon : 0,
		                          &eai);
		if (naddrs < 0) return eai;
		if (naddrs == 0) return EAI_NONAME;
		if (flags & AI_CANONNAME) have_canon = 1;
	}

	{
		int want_stream = (socktype == 0 || socktype == SOCK_STREAM);
		int want_dgram = (socktype == SOCK_DGRAM);

		for (i = 0; i < naddrs; i++) {
			int variants[2], nv = 0, v;

			/* Unconstrained ai_socktype: SOCK_STREAM entries
			 * only, not one of each -- see include/netdb.h's
			 * own banner for why this is a disclosed
			 * simplification rather than the full "one entry
			 * per supported socket type" DESCRIPTION allows. */
			if (want_stream) variants[nv++] = SOCK_STREAM;
			if (want_dgram) variants[nv++] = SOCK_DGRAM;

			for (v = 0; v < nv; v++) {
				int proto = variants[v] == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
				struct addrinfo *ai = make_node(variants[v], proto, addrs[i], port);

				if (!ai) { freeaddrinfo(head); return EAI_MEMORY; }
				if (!head) head = ai; else tail->ai_next = ai;
				tail = ai;
			}
		}
	}

	if (!head) return EAI_NONAME;

	if (have_canon) {
		head->ai_canonname = strdup(canon);
		if (!head->ai_canonname) { freeaddrinfo(head); return EAI_MEMORY; }
	}
	head->ai_flags = flags;

	*res = head;
	return 0;
}

const char *gai_strerror(int code)
{
	switch (code) {
	case 0:             return "Success";
	case EAI_AGAIN:     return "Temporary failure in name resolution";
	case EAI_BADFLAGS:  return "Invalid value for ai_flags";
	case EAI_FAIL:      return "Non-recoverable failure in name resolution";
	case EAI_FAMILY:    return "ai_family not supported";
	case EAI_MEMORY:    return "Memory allocation failure";
	case EAI_NONAME:    return "Name or service not known";
	case EAI_SERVICE:   return "Servname not supported for ai_socktype";
	case EAI_SOCKTYPE:  return "ai_socktype not supported";
	case EAI_SYSTEM:    return "System error";
	case EAI_OVERFLOW:  return "Argument buffer overflow";
	default:            return "Unknown error";
	}
}
