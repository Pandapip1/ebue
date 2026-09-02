/* C library headers must use the implementation-reserved namespace for guards,
 * type plumbing, and implementation extensions so they cannot collide with users.
 */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)

/* SPDX-FileCopyrightText: (C) 2026 Gavin John
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * <netdb.h>: https://pubs.opengroup.org/onlinepubs/9699919799/
 * basedefs/netdb.h.html, functions/freeaddrinfo.html (which also
 * specifies getaddrinfo), functions/gai_strerror.html. See
 * test/posix-netdb.c's own header comment for which <netdb.h>
 * interfaces this edition of POSIX actually mandates (getnameinfo()
 * and the four database families -- host/network/protocol/service --
 * are POSIX-mandatory too; gethostbyname()/gethostbyaddr()/h_errno are
 * NOT, having been removed from this edition).
 *
 * ntlibc implements its own statically-linked "files"/"dns" NSS
 * services -- a real hosts-file backend, a minimal UDP DNS stub
 * resolver, and an /etc/nsswitch.conf parser choosing between them
 * (see src/netdb/linux/nsswitch.c's own banner) -- rather than
 * dlopen()ing glibc's own libnss_*.so.2. gethostbyname() is kept as a
 * disclosed legacy/XSI extension outside this POSIX edition, since it
 * remains a common way real programs ask "what is this host's
 * address", and it is a thin second front door onto the same
 * __hosts_resolve() walk getaddrinfo() itself uses, not a second
 * resolver.
 *
 * getnameinfo() (src/netdb/linux/getnameinfo.c) does a real reverse
 * /etc/hosts walk before falling back to numeric, but sends no PTR DNS
 * query, so a name that exists only in DNS (never in the local hosts
 * file) still falls back to numeric -- see that file's own banner. The
 * four enumerable databases (host/network/protocol/service) are real
 * /etc/hosts(5)/etc/networks(5)/etc/protocols(5)/etc/services(5)
 * parsers (src/netdb/linux/hostent.c, networks.c, protocols.c,
 * services.c), each treating a missing file as a clean empty database
 * rather than an error (see networks.c's own banner for why that
 * matters most for /etc/networks specifically: most real machines have
 * none).
 *
 * No IPv6 transport: this project's socket layer has no IPv6 support
 * (see <sys/socket.h>'s own banner), so getaddrinfo() with
 * ai_family == AF_INET6 fails cleanly with EAI_FAMILY rather than
 * silently degrading to IPv4 or fabricating an unusable IPv6 result.
 * AF_UNSPEC (the default) resolves to IPv4 only -- conformant, since
 * DESCRIPTION only requires addresses "for each of the address
 * families that comply with the ai_family value", and IPv6 does not
 * comply with anything this implementation offers.
 *
 * struct addrinfo's ai_addr/ai_canonname are heap-owned by
 * getaddrinfo() and released by freeaddrinfo(); see
 * src/netdb/linux/addrinfo.c for the exact allocation shape.
 */
#ifndef _NETDB_H
#define _NETDB_H
#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define __NEED_size_t
#include <bits/alltypes.h>

/* netdb.h.html: "struct hostent" -- the five members POSIX mandates.
 * h_addrtype/h_length describe every entry in h_addr_list uniformly
 * (this implementation only ever fills AF_INET/4, see this header's
 * own banner), matching every real implementation's own layout. */
struct hostent {
	char *h_name;
	char **h_aliases;
	int h_addrtype;
	int h_length;
	char **h_addr_list;
};

/* netdb.h.html: "struct addrinfo". Member order is POSIX's own
 * canonical order, not load-bearing, but kept for readability against
 * the spec text. */
struct addrinfo {
	int ai_flags;
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	socklen_t ai_addrlen;
	struct sockaddr *ai_addr;
	char *ai_canonname;
	struct addrinfo *ai_next;
};

/* netdb.h.html AI_* flags for addrinfo.ai_flags / hints.ai_flags.
 * AI_PASSIVE, AI_CANONNAME and AI_NUMERICHOST are honored for real by
 * src/netdb/linux/addrinfo.c; AI_NUMERICSERV is honored too (this
 * implementation only ever accepts a numeric service string in the
 * first place -- see that file's own comment on EAI_SERVICE).
 * AI_V4MAPPED/AI_ALL/AI_ADDRCONFIG are declared for header
 * completeness but have no observable effect: they only modify
 * AF_INET6 behavior, and this implementation never returns AF_INET6
 * results at all (see this header's own banner). */
#define AI_PASSIVE     0x0001
#define AI_CANONNAME   0x0002
#define AI_NUMERICHOST 0x0004
#define AI_NUMERICSERV 0x0008
#define AI_V4MAPPED    0x0010
#define AI_ALL         0x0020
#define AI_ADDRCONFIG  0x0040

/* netdb.h.html NI_* flags for getnameinfo() (src/netdb/linux/
 * getnameinfo.c, src/netdb/nt/plat_netdb.c). NI_NUMERICHOST/
 * NI_NUMERICSERV are always honored exactly (no database is consulted
 * at all for either); NI_NAMEREQD is honored on Linux (a name that
 * cannot be found makes the call fail rather than silently falling back
 * to numeric); NI_DGRAM selects "udp" over "tcp" when resolving a
 * non-numeric service name. NI_NOFQDN is declared for header
 * completeness but has no observable effect: nothing in this
 * implementation's /etc/hosts walk ever returns more of a name than the
 * file itself already spells out, so there is no "trim the domain
 * suffix" transformation to apply either way. */
#define NI_NOFQDN       0x0001
#define NI_NUMERICHOST  0x0002
#define NI_NAMEREQD     0x0004
#define NI_NUMERICSERV  0x0008
#define NI_DGRAM        0x0010
#define NI_MAXHOST      1025
#define NI_MAXSERV      32

/* netdb.h.html EAI_* getaddrinfo()/getnameinfo() error codes -- POSIX
 * requires only that gai_strerror() (src/netdb/linux/addrinfo.c) map
 * each to a distinct, non-empty string and that these names exist;
 * exact numeric values are this implementation's own, matching no
 * external ABI (getaddrinfo() callers are required to treat these as
 * opaque per DESCRIPTION, never as a raw errno-shaped integer). */
#define EAI_AGAIN     (-3)  /* the name could not be resolved at this time */
#define EAI_BADFLAGS  (-1)  /* the flags parameter had an invalid value */
#define EAI_FAIL      (-4)  /* a non-recoverable error occurred */
#define EAI_FAMILY    (-6)  /* the address family was not recognized, or the address length was invalid for the specified family */
#define EAI_MEMORY    (-10) /* memory allocation failure */
#define EAI_NONAME    (-2)  /* the name does not resolve for the supplied parameters */
#define EAI_SERVICE   (-8)  /* the service passed was not recognized for the specified socket type */
#define EAI_SOCKTYPE  (-7)  /* the intended socket type was not recognized */
#define EAI_SYSTEM    (-11) /* a system error occurred; the error code is in errno */
#define EAI_OVERFLOW  (-12) /* an argument buffer overflowed */

int getaddrinfo(const char *__restrict, const char *__restrict,
                 const struct addrinfo *__restrict, struct addrinfo **__restrict)
    __attribute__((nonnull(4)));
void freeaddrinfo(struct addrinfo *);
const char *gai_strerror(int);

/* getnameinfo(): the reverse of getaddrinfo() -- see this header's own
 * banner for exactly what each platform backend does (Linux: a real
 * /etc/hosts reverse walk plus a real /etc/services lookup, falling
 * back to numeric per DESCRIPTION whenever a name genuinely cannot be
 * found; NT: numeric-only, honestly, see src/netdb/nt/plat_netdb.c's
 * own banner). sa required: every real caller passes a real sockaddr,
 * and DESCRIPTION gives no meaning to a NULL one -- there is no address
 * to report on at all without it. */
int getnameinfo(const struct sockaddr *__restrict, socklen_t,
                 char *__restrict, socklen_t, char *__restrict, socklen_t, int)
    __attribute__((nonnull(1)));

/* gethostbyname(): XSI/legacy, removed from this edition of POSIX
 * (test/posix-netdb.c's own header comment records the check), kept
 * as a real, disclosed extension -- see this header's own banner.
 * Non-reentrant per its own historical contract (the same "need not be
 * thread-safe" shape src/misc/pwd.c's getpwnam() already documents);
 * src/netdb/linux/hostent.c is the only body. name required: every
 * real caller passes a real string, and this function's own
 * current_name()-shaped lookup dereferences it unconditionally before
 * any NULL check would matter (a NULL name cannot be a hostname). */
struct hostent *gethostbyname(const char *)
    __attribute__((nonnull(1)));

/* sethostent()/gethostent()/endhostent(): endhostent.html's sequential
 * host-database walk -- one struct hostent per line of the same
 * /etc/hosts gethostbyname() above already reads, in file order rather
 * than by name. src/netdb/linux/hostent.c is the real Linux body;
 * src/netdb/nt/plat_netdb.c's own banner explains its NT behavior. */
void sethostent(int);
struct hostent *gethostent(void);
void endhostent(void);

/* netdb.h.html: "struct netent" -- the four members POSIX mandates. */
struct netent {
	char *n_name;
	char **n_aliases;
	int n_addrtype;
	uint32_t n_net;
};

/* setnetent()/getnetent()/endnetent()/getnetbyname()/getnetbyaddr():
 * endnetent.html's network database -- see src/netdb/linux/networks.c's
 * own banner for the real Linux /etc/networks(5) parser (and for why
 * that file being routinely absent is this database's own normal empty
 * state, not an error) and src/netdb/nt/plat_netdb.c's banner for NT. */
void setnetent(int);
struct netent *getnetent(void);
void endnetent(void);
struct netent *getnetbyname(const char *)
    __attribute__((nonnull(1)));
struct netent *getnetbyaddr(uint32_t, int);

/* netdb.h.html: "struct protoent" -- the three members POSIX mandates. */
struct protoent {
	char *p_name;
	char **p_aliases;
	int p_proto;
};

/* setprotoent()/getprotoent()/endprotoent()/getprotobyname()/
 * getprotobynumber(): endprotoent.html's protocol database -- see
 * src/netdb/linux/protocols.c's own banner for the real Linux
 * /etc/protocols(5) parser and src/netdb/nt/plat_netdb.c's banner for
 * NT. */
void setprotoent(int);
struct protoent *getprotoent(void);
void endprotoent(void);
struct protoent *getprotobyname(const char *)
    __attribute__((nonnull(1)));
struct protoent *getprotobynumber(int);

/* netdb.h.html: "struct servent" -- the four members POSIX mandates. */
struct servent {
	char *s_name;
	char **s_aliases;
	int s_port;
	char *s_proto;
};

/* setservent()/getservent()/endservent()/getservbyname()/
 * getservbyport(): endservent.html's service database -- see
 * src/netdb/linux/services.c's own banner for the real Linux
 * /etc/services(5) parser and src/netdb/nt/plat_netdb.c's banner for
 * NT. name/proto and port/proto are each individually optional per
 * DESCRIPTION ("If proto is a null pointer, any value of the s_proto
 * member shall be matched"), so only the non-nullable positional
 * arguments (name, port) carry nonnull. */
void setservent(int);
struct servent *getservent(void);
void endservent(void);
struct servent *getservbyname(const char *, const char *)
    __attribute__((nonnull(1)));
struct servent *getservbyport(int, const char *);

/* h_errno: gethostbyname()'s own non-POSIX error-reporting channel,
 * exactly as legacy as gethostbyname() itself -- kept for the same
 * reason (real programs that already call gethostbyname() routinely
 * also read h_errno on failure, and a gethostbyname() with no way to
 * distinguish "not found" from "server down" would be a materially
 * worse extension than the one being added). herror()/hstrerror() are
 * not provided. */
extern int h_errno;
#define HOST_NOT_FOUND 1
#define TRY_AGAIN      2
#define NO_RECOVERY    3
#define NO_DATA        4

#ifdef __cplusplus
}
#endif
#endif

// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp)
